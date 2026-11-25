// Copyright 2026 davenonymous
#include "smx/smx_file.h"

#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace smrunner {

namespace {

constexpr uint32_t kMagic = 0x53504646;
constexpr uint8_t kCompressionGz = 1;
// SMX versions the VM loads (smx-headers.h: SP1_VERSION_1_0, _1_1, _1_7).
constexpr uint16_t kVersions[] = {0x0101, 0x0102, 0x0107};
// No plugin comes near this; a header that claims more is broken.
constexpr uint32_t kMaxImageSize = 256u * 1024 * 1024;

// Type signature bytes (smx-typeinfo.h, namespace cb).
constexpr uint8_t kBool = 0x01;
constexpr uint8_t kInt32 = 0x06;
constexpr uint8_t kFloat32 = 0x0c;
constexpr uint8_t kChar8 = 0x0e;
constexpr uint8_t kAny = 0x10;
constexpr uint8_t kTopFunction = 0x11;
constexpr uint8_t kFixedArray = 0x30;
constexpr uint8_t kArray = 0x31;
constexpr uint8_t kFunction = 0x32;
constexpr uint8_t kEnum = 0x42;
constexpr uint8_t kTypedef = 0x43;
constexpr uint8_t kTypeset = 0x44;
constexpr uint8_t kClassdef = 0x45;
constexpr uint8_t kEnumStruct = 0x46;
constexpr uint8_t kVoid = 0x70;
constexpr uint8_t kLegacyVariadic = 0x71;
constexpr uint8_t kByRef = 0x72;
constexpr uint8_t kConst = 0x73;

constexpr uint8_t kVarClassLocal = 1;
constexpr uint8_t kVarClassStatic = 2;
constexpr uint8_t kVarClassArg = 3;

#pragma pack(push, 1)
struct FileHeader {
	uint32_t magic;
	uint16_t version;
	uint8_t compression;
	uint32_t disksize;
	uint32_t imagesize;
	uint8_t sections;
	uint32_t stringtab;
	uint32_t dataoffs;
};
struct Section {
	uint32_t nameoffs;
	uint32_t dataoffs;
	uint32_t size;
};
struct TableHeader {
	uint32_t headerSize;
	uint32_t rowSize;
	uint32_t rowCount;
};
struct DataHeader {
	uint32_t datasize;
	uint32_t memsize;
	uint32_t data;
};
struct PublicRow {
	uint32_t address;
	uint32_t name;
};
struct MethodRow {
	uint32_t name;
	uint32_t pcodeStart;
	uint32_t pcodeEnd;
	uint32_t signature;
};
struct DebugMethodRow {
	uint32_t methodIndex;
	uint32_t firstLocal;
};
struct DebugVarRow {
	int32_t address;
	uint8_t vclass;
	uint32_t name;
	uint32_t codeStart;
	uint32_t codeEnd;
	uint32_t typeId;
};
struct NamedRow {
	uint32_t name;
};
#pragma pack(pop)

// The decompressed image with bounds-checked access.
class Image {
 public:
	explicit Image(std::string bytes) : bytes_(std::move(bytes)) {}

	bool Has(size_t offset, size_t length) const { return offset <= bytes_.size() && length <= bytes_.size() - offset; }
	template <typename T>
	bool Read(size_t offset, T* out) const {
		if (!Has(offset, sizeof(T))) {
			return false;
		}
		memcpy(out, bytes_.data() + offset, sizeof(T));
		return true;
	}
	std::string String(size_t offset, size_t limit) const {
		if (offset >= limit || limit > bytes_.size()) {
			return std::string();
		}
		const char* start = bytes_.data() + offset;
		size_t length = strnlen(start, limit - offset);
		return std::string(start, length);
	}
	uint8_t Byte(size_t offset) const { return offset < bytes_.size() ? static_cast<uint8_t>(bytes_[offset]) : 0; }
	std::string& bytes() { return bytes_; }

 private:
	std::string bytes_;
};

struct SectionInfo {
	uint32_t offset;
	uint32_t size;
};

class Reader {
 public:
	Reader(Image& image, std::map<std::string, SectionInfo> sections)
		: image_(image), sections_(std::move(sections)) {}

	bool HasSection(const std::string& name) const { return sections_.count(name) != 0; }
	const SectionInfo* Find(const std::string& name) const {
		auto found = sections_.find(name);
		return found == sections_.end() ? nullptr : &found->second;
	}

	std::string Name(uint32_t index) const {
		const SectionInfo* names = Find(".names");
		if (!names || index >= names->size) {
			return std::string();
		}
		return image_.String(names->offset + index, names->offset + names->size);
	}

	// Reads the rows of an RTTI table into T, for rows at least sizeof(T).
	template <typename T>
	std::vector<T> Table(const std::string& name) const {
		std::vector<T> rows;
		const SectionInfo* section = Find(name);
		TableHeader header;
		if (!section || !image_.Read(section->offset, &header) || header.rowSize < sizeof(T)) {
			return rows;
		}
		for (uint32_t i = 0; i < header.rowCount; i++) {
			size_t at = section->offset + header.headerSize + static_cast<size_t>(i) * header.rowSize;
			if (at + header.rowSize > section->offset + section->size) {
				break;
			}
			T row;
			if (!image_.Read(at, &row)) {
				break;
			}
			rows.push_back(row);
		}
		return rows;
	}

	// The by-reference flags of a method signature's parameters (see
	// smx_rtti_method::signature): FormalArgs, LegacyVariadic?, ReturnType,
	// then kByRef? <type> per parameter. Arrays count as by reference.
	std::vector<bool> ByRefArguments(uint32_t signature) const {
		std::vector<bool> flags;
		const SectionInfo* data = Find("rtti.data");
		if (!data || signature >= data->size) {
			return flags;
		}
		std::vector<uint8_t> bytes;
		for (uint32_t i = signature; i < data->size && i < signature + 512; i++) {
			bytes.push_back(image_.Byte(data->offset + i));
		}
		size_t position = 0;
		uint8_t count = bytes[position++];
		if (position < bytes.size() && bytes[position] == kLegacyVariadic) {
			position++;
		}
		SmxType ignored;
		if (position < bytes.size() && bytes[position] == kVoid) {
			position++;
		} else if (!Decode(bytes, position, ignored)) {
			return std::vector<bool>();
		}
		for (uint8_t i = 0; i < count; i++) {
			bool byRef = position < bytes.size() && bytes[position] == kByRef;
			if (byRef) {
				position++;
			}
			SmxType type;
			if (!Decode(bytes, position, type)) {
				return std::vector<bool>();
			}
			flags.push_back(byRef || !type.dims.empty());
		}
		return flags;
	}

	// Decodes a type id; Other when it cannot.
	SmxType Type(uint32_t typeId) const {
		std::vector<uint8_t> bytes;
		size_t position = 0;
		if ((typeId & 0xf) == 0) {
			// The VM decodes all four bytes of an inline payload (vm/rtti.cpp);
			// a zero byte can be an index, as in kEnum 0x00.
			uint32_t payload = typeId >> 4;
			for (int i = 0; i < 4; i++) {
				bytes.push_back(static_cast<uint8_t>((payload >> (8 * i)) & 0xff));
			}
		} else if ((typeId & 0xf) == 1) {
			const SectionInfo* data = Find("rtti.data");
			if (!data) {
				return SmxType();
			}
			uint32_t offset = typeId >> 4;
			for (uint32_t i = offset; i < data->size && i < offset + 64; i++) {
				bytes.push_back(image_.Byte(data->offset + i));
			}
		}
		SmxType type;
		if (!Decode(bytes, position, type)) {
			return SmxType();
		}
		return type;
	}

 private:
	static bool DecodeUint32(const std::vector<uint8_t>& bytes, size_t& position, uint32_t& value) {
		value = 0;
		for (uint32_t shift = 0; shift < 35; shift += 7) {
			if (position >= bytes.size()) {
				return false;
			}
			uint8_t b = bytes[position++];
			value |= static_cast<uint32_t>(b & 0x7f) << shift;
			if ((b & 0x80) == 0) {
				return true;
			}
		}
		return false;
	}

	bool Decode(const std::vector<uint8_t>& bytes, size_t& position, SmxType& type) const {
		if (position < bytes.size() && bytes[position] == kConst) {
			type.isConst = true;
			position++;
		}
		if (position >= bytes.size()) {
			return false;
		}
		uint8_t code = bytes[position++];
		uint32_t value;
		switch (code) {
			case kBool: type.base = SmxType::Base::Bool; return true;
			case kInt32: type.base = SmxType::Base::Int; return true;
			case kFloat32: type.base = SmxType::Base::Float; return true;
			case kChar8: type.base = SmxType::Base::Char; return true;
			case kAny: type.base = SmxType::Base::Any; return true;
			case kTopFunction: type.base = SmxType::Base::Function; return true;
			case kFunction: {
				type.base = SmxType::Base::Function;
				if (position >= bytes.size()) {
					return false;
				}
				uint8_t count = bytes[position++];
				if (position < bytes.size() && bytes[position] == kLegacyVariadic) {
					position++;
				}
				SmxType inner;
				if (position < bytes.size() && bytes[position] == kVoid) {
					position++;
				} else if (!Decode(bytes, position, inner)) {
					return false;
				}
				for (uint8_t i = 0; i < count; i++) {
					if (position < bytes.size() && bytes[position] == kByRef) {
						position++;
					}
					SmxType parameter;
					if (!Decode(bytes, position, parameter)) {
						return false;
					}
				}
				return true;
			}
			case kFixedArray:
				if (!DecodeUint32(bytes, position, value)) {
					return false;
				}
				type.dims.push_back(value);
				return Decode(bytes, position, type);
			case kArray:
				type.dims.push_back(0);
				return Decode(bytes, position, type);
			case kEnum:
				if (!DecodeUint32(bytes, position, value)) {
					return false;
				}
				type.base = SmxType::Base::Enum;
				type.name = TableName("rtti.enums", value);
				return true;
			case kEnumStruct:
				if (!DecodeUint32(bytes, position, value)) {
					return false;
				}
				type.base = SmxType::Base::EnumStruct;
				type.name = TableName("rtti.enumstructs", value);
				return true;
			case kTypedef:
			case kTypeset:
				// Function types: one cell, the function's id.
				if (!DecodeUint32(bytes, position, value)) {
					return false;
				}
				type.base = SmxType::Base::Function;
				return true;
			case kClassdef:
				// A pstruct (methodmaps are kEnum): fields the runner does not read.
				if (!DecodeUint32(bytes, position, value)) {
					return false;
				}
				type.base = SmxType::Base::Other;
				return true;
			default:
				return false;
		}
	}

	std::string TableName(const std::string& table, uint32_t index) const {
		std::vector<NamedRow> rows = Table<NamedRow>(table);
		return index < rows.size() ? Name(rows[index].name) : std::string();
	}

	Image& image_;
	std::map<std::string, SectionInfo> sections_;
};

SmxVariable::Scope ScopeOf(uint8_t vclass) {
	switch (vclass & 0x3) {
		case kVarClassLocal: return SmxVariable::Scope::Local;
		case kVarClassStatic: return SmxVariable::Scope::Static;
		case kVarClassArg: return SmxVariable::Scope::Argument;
		default: return SmxVariable::Scope::Global;
	}
}

}  // namespace

std::string SmxType::Describe() const {
	std::string text = isConst ? "const " : "";
	switch (base) {
		case Base::Bool: text += "bool"; break;
		case Base::Int: text += "int"; break;
		case Base::Float: text += "float"; break;
		case Base::Char: text += "char"; break;
		case Base::Any: text += "any"; break;
		case Base::Function: text += "Function"; break;
		case Base::Enum: text += name.empty() ? "enum" : name; break;
		case Base::EnumStruct: text += name.empty() ? "enum struct" : name; break;
		case Base::Other: text += "?"; break;
	}
	for (uint32_t dim : dims) {
		text += dim ? "[" + std::to_string(dim) + "]" : "[]";
	}
	return text;
}

std::unique_ptr<SmxFile> SmxFile::Load(const std::string& path, std::string* error) {
	auto fail = [error, &path](const std::string& reason) -> std::unique_ptr<SmxFile> {
		if (error) {
			*error = path + ": " + reason;
		}
		return nullptr;
	};
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return fail("cannot be read");
	}
	std::stringstream content;
	content << in.rdbuf();
	Image image(content.str());

	FileHeader header;
	if (!image.Read(0, &header) || header.magic != kMagic) {
		return fail("is not a SourcePawn plugin");
	}
	if (std::find(std::begin(kVersions), std::end(kVersions), header.version) == std::end(kVersions)) {
		char version[16];
		snprintf(version, sizeof(version), "0x%04x", header.version);
		return fail(std::string("has an SMX version the VM does not load (") + version + ")");
	}
	if (header.imagesize > kMaxImageSize) {
		return fail("claims an image of " + std::to_string(header.imagesize) + " bytes");
	}
	if (header.compression == kCompressionGz) {
		if (header.dataoffs > header.disksize || !image.Has(header.dataoffs, header.disksize - header.dataoffs) ||
		    header.imagesize < header.dataoffs) {
			return fail("has a broken compressed region");
		}
		std::string expanded(header.imagesize, '\0');
		memcpy(&expanded[0], image.bytes().data(), header.dataoffs);
		uLongf length = header.imagesize - header.dataoffs;
		int result = uncompress(reinterpret_cast<Bytef*>(&expanded[header.dataoffs]), &length,
		                        reinterpret_cast<const Bytef*>(image.bytes().data() + header.dataoffs),
		                        header.disksize - header.dataoffs);
		if (result != Z_OK) {
			return fail("cannot be decompressed");
		}
		image.bytes() = std::move(expanded);
	}

	std::map<std::string, SectionInfo> sections;
	for (uint8_t i = 0; i < header.sections; i++) {
		Section section;
		if (!image.Read(sizeof(FileHeader) + i * sizeof(Section), &section) ||
		    !image.Has(section.dataoffs, section.size)) {
			return fail("has a broken section table");
		}
		std::string name = image.String(header.stringtab + section.nameoffs, image.bytes().size());
		sections[name] = SectionInfo{section.dataoffs, section.size};
	}
	Reader reader(image, sections);
	auto file = std::unique_ptr<SmxFile>(new SmxFile());

	if (const SectionInfo* publics = reader.Find(".publics")) {
		for (uint32_t at = 0; at + sizeof(PublicRow) <= publics->size; at += sizeof(PublicRow)) {
			PublicRow row;
			image.Read(publics->offset + at, &row);
			file->publics_.push_back(reader.Name(row.name));
		}
	}

	std::vector<MethodRow> methods = reader.Table<MethodRow>("rtti.methods");
	for (const MethodRow& method : methods) {
		SmxFunction function;
		function.name = reader.Name(method.name);
		function.codeStart = method.pcodeStart;
		function.codeEnd = method.pcodeEnd;
		function.isPublic =
			std::find(file->publics_.begin(), file->publics_.end(), function.name) != file->publics_.end();
		function.byRefArguments = reader.ByRefArguments(method.signature);
		file->functions_.push_back(function);
	}

	uint32_t memsize = 0;
	if (const SectionInfo* data = reader.Find(".data")) {
		DataHeader dataHeader;
		if (image.Read(data->offset, &dataHeader)) {
			memsize = dataHeader.memsize;
		}
	}
	auto toVariable = [&reader](const DebugVarRow& row) {
		SmxVariable variable;
		variable.name = reader.Name(row.name);
		variable.scope = ScopeOf(row.vclass);
		variable.address = row.address;
		variable.codeStart = row.codeStart;
		variable.codeEnd = row.codeEnd;
		variable.type = reader.Type(row.typeId);
		return variable;
	};
	auto hasStorage = [memsize](const SmxVariable& variable) {
		if (variable.scope == SmxVariable::Scope::Global || variable.scope == SmxVariable::Scope::Static) {
			return variable.address >= 0 && static_cast<uint32_t>(variable.address) < memsize;
		}
		return true;
	};

	for (const DebugVarRow& row : reader.Table<DebugVarRow>(".dbg.globals")) {
		SmxVariable variable = toVariable(row);
		if (!variable.name.empty() && hasStorage(variable)) {
			file->globals_.push_back(variable);
		}
	}

	std::vector<DebugVarRow> locals = reader.Table<DebugVarRow>(".dbg.locals");
	std::vector<DebugMethodRow> debugMethods = reader.Table<DebugMethodRow>(".dbg.methods");
	for (size_t i = 0; i < debugMethods.size(); i++) {
		uint32_t methodIndex = debugMethods[i].methodIndex;
		if (methodIndex >= file->functions_.size()) {
			continue;
		}
		uint32_t first = debugMethods[i].firstLocal;
		uint32_t last = i + 1 < debugMethods.size() ? debugMethods[i + 1].firstLocal : static_cast<uint32_t>(locals.size());
		for (uint32_t local = first; local < last && local < locals.size(); local++) {
			SmxVariable variable = toVariable(locals[local]);
			if (!variable.name.empty() && hasStorage(variable)) {
				file->functions_[methodIndex].locals.push_back(variable);
			}
		}
	}
	return file;
}

const SmxFunction* SmxFile::FunctionAt(uint32_t cip) const {
	for (const SmxFunction& function : functions_) {
		if (cip >= function.codeStart && cip < function.codeEnd) {
			return &function;
		}
	}
	return nullptr;
}

const SmxFunction* SmxFile::FindFunction(const std::string& name) const {
	for (const SmxFunction& function : functions_) {
		if (function.name == name) {
			return &function;
		}
	}
	return nullptr;
}

const SmxVariable* SmxFile::FindGlobal(const std::string& name) const {
	for (const SmxVariable& variable : globals_) {
		if (variable.name == name) {
			return &variable;
		}
	}
	return nullptr;
}

const SmxVariable* SmxFile::FindLocal(uint32_t cip, const std::string& name) const {
	const SmxFunction* function = FunctionAt(cip);
	if (!function) {
		return nullptr;
	}
	// Later entries are the inner scopes; the innermost visible one wins.
	// Arguments hold their values from the function's first instruction on,
	// even where their debug scope has not begun yet.
	const SmxVariable* found = nullptr;
	for (const SmxVariable& variable : function->locals) {
		bool visible = variable.scope == SmxVariable::Scope::Argument ||
		               (cip >= variable.codeStart && cip <= variable.codeEnd);
		if (variable.name == name && visible) {
			found = &variable;
		}
	}
	return found;
}

}  // namespace smrunner
