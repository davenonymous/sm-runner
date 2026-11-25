// Copyright 2026 davenonymous
#include "recorder.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace steamworks {

using nlohmann::ordered_json;

// Headers a replayed answer must not repeat: the connection's, the date of
// the recording, and the length the stand-in sets itself from the body
// file. A HEAD answer has no body file, so it keeps its Content-Length.
static bool IsKeptHeader(const std::string& name, Method method) {
	static const char* const kDropped[] = {"Date", "Connection", "Transfer-Encoding", "Keep-Alive"};
	for (const char* dropped : kDropped) {
		if (strcasecmp(name.c_str(), dropped) == 0) {
			return false;
		}
	}
	return method == Method::Head || strcasecmp(name.c_str(), "Content-Length") != 0;
}

static std::string LowerCase(std::string text) {
	for (char& c : text) {
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	return text;
}

static std::string SafeSegment(const std::string& segment) {
	if (segment == "." || segment == "..") {
		return "_";
	}
	std::string safe;
	for (unsigned char c : segment) {
		safe += isalnum(c) || c == '.' || c == '-' || c == '_' ? static_cast<char>(c) : '_';
	}
	return safe;
}

static std::string ExtensionFor(const std::string& contentType) {
	static const std::pair<const char*, const char*> kExtensions[] = {
		{"application/json", ".json"}, {"text/plain", ".txt"}, {"text/html", ".html"}, {"application/xml", ".xml"},
	};
	std::string type = contentType.substr(0, contentType.find(';'));
	while (!type.empty() && isspace(static_cast<unsigned char>(type.back()))) {
		type.pop_back();
	}
	for (const auto& extension : kExtensions) {
		if (strcasecmp(type.c_str(), extension.first) == 0) {
			return extension.second;
		}
	}
	return ".bin";
}

// FNV-1a, 32 bits: the same name for the same query on every build.
static std::string QueryHash(const std::string& query) {
	uint32_t hash = 2166136261u;
	for (unsigned char c : query) {
		hash = (hash ^ c) * 16777619u;
	}
	char hex[9];
	snprintf(hex, sizeof(hex), "%08x", hash);
	return hex;
}

std::string BodyFileName(const std::string& url, const std::string& contentType, Method method) {
	std::optional<Url> parsed = ParseUrl(url);
	if (!parsed) {
		throw std::runtime_error("cannot record the answer of " + url + ": not an http(s) URL");
	}
	std::string name = "bodies/" + SafeSegment(parsed->host);
	std::string path = parsed->path;
	if (path.back() == '/') {
		path += "index";
	}
	std::string last;
	size_t start = 1;
	while (start < path.size()) {
		size_t slash = path.find('/', start);
		std::string segment = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (!segment.empty()) {
			if (!last.empty()) {
				name += "/" + last;
			}
			last = SafeSegment(segment);
		}
		if (slash == std::string::npos) {
			break;
		}
		start = slash + 1;
	}
	if (last.empty()) {
		last = "index";
	}
	// The path's own extension stays last; without one the Content-Type's.
	size_t dot = last.rfind('.');
	bool hasExtension = dot != std::string::npos && dot > 0;
	std::string stem = hasExtension ? last.substr(0, dot) : last;
	std::string extension = hasExtension ? last.substr(dot) : ExtensionFor(contentType);
	std::string hash = parsed->query.empty() ? "" : "_" + QueryHash(parsed->query);
	// Answers of other methods to the same URL have files of their own;
	// SafeSegment never makes an '@', so no path can look like a method.
	std::string methodPart = method == Method::Get ? "" : "@" + LowerCase(MethodName(method));
	return name + "/" + stem + hash + methodPart + extension;
}

static void MakeParents(const std::string& path) {
	for (size_t slash = path.find('/', 1); slash != std::string::npos; slash = path.find('/', slash + 1)) {
		std::string parent = path.substr(0, slash);
		if (mkdir(parent.c_str(), 0755) != 0 && errno != EEXIST) {
			throw std::runtime_error("cannot make the folder " + parent + ": " + strerror(errno));
		}
	}
}

// Writes through a temporary file renamed onto the target.
static void WriteFile(const std::string& path, const std::string& content) {
	MakeParents(path);
	std::string temporary = path + ".tmp";
	{
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		out << content;
		if (!out.flush()) {
			throw std::runtime_error("cannot write " + temporary);
		}
	}
	if (rename(temporary.c_str(), path.c_str()) != 0) {
		throw std::runtime_error("cannot rename " + temporary + " to " + path + ": " + strerror(errno));
	}
}

// name, or, when another entry's body file has it, name with ~2, ~3, ...
// before its extension; neither SafeSegment nor the method mark makes a '~',
// so such a name is no other request's own name either.
static std::string UnusedBodyFileName(const std::string& name, const std::set<std::string>& used) {
	if (used.count(name) == 0) {
		return name;
	}
	size_t dot = name.rfind('.');
	size_t slash = name.rfind('/');
	bool hasExtension = dot != std::string::npos && (slash == std::string::npos || dot > slash);
	std::string stem = hasExtension ? name.substr(0, dot) : name;
	std::string extension = hasExtension ? name.substr(dot) : "";
	for (int number = 2;; number++) {
		std::string candidate = stem + "~" + std::to_string(number) + extension;
		if (used.count(candidate) == 0) {
			return candidate;
		}
	}
}

// Holds an exclusive lock on DIR/steamworks.json.lock while it lives.
class FolderLock {
 public:
	explicit FolderLock(const std::string& dir) {
		std::string path = dir + "/steamworks.json.lock";
		fd_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
		if (fd_ < 0 || flock(fd_, LOCK_EX) != 0) {
			throw std::runtime_error("cannot lock " + path + ": " + strerror(errno));
		}
	}
	~FolderLock() {
		if (fd_ >= 0) {
			close(fd_);
		}
	}
	FolderLock(const FolderLock&) = delete;
	FolderLock& operator=(const FolderLock&) = delete;

 private:
	int fd_ = -1;
};

static ordered_json ReadEntries(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return ordered_json::array();
	}
	std::ostringstream content;
	content << in.rdbuf();
	ordered_json document = ordered_json::parse(content.str(), nullptr, false);
	if (document.is_discarded() || !document.is_array()) {
		throw std::runtime_error("cannot record into " + path + ": it is not a JSON list of entries");
	}
	return document;
}

void RecordAnswer(const std::string& dir, const RecordedAnswer& answer) {
	FolderLock lock(dir);
	std::string mappingFile = dir + "/steamworks.json";
	ordered_json entries = ReadEntries(mappingFile);

	// The entries of other requests stay; their body files are taken.
	ordered_json kept = ordered_json::array();
	std::set<std::string> usedBodyFiles;
	for (const auto& existing : entries) {
		bool sameRequest = existing.is_object() && existing.value("url", "") == answer.url &&
		                   strcasecmp(existing.value("method", "GET").c_str(), MethodName(answer.method)) == 0;
		if (sameRequest) {
			continue;
		}
		kept.push_back(existing);
		if (existing.is_object() && existing.contains("body_file") && existing["body_file"].is_string()) {
			usedBodyFiles.insert(existing["body_file"].get<std::string>());
		}
	}

	ordered_json headers = ordered_json::object();
	std::string contentType;
	for (const auto& header : answer.headers) {
		if (strcasecmp(header.first.c_str(), "Content-Type") == 0) {
			contentType = header.second;
		}
		if (IsKeptHeader(header.first, answer.method)) {
			headers[header.first] = header.second;
		}
	}
	ordered_json entry = ordered_json::object();
	entry["method"] = MethodName(answer.method);
	entry["url"] = answer.url;
	entry["status"] = answer.status;
	if (!headers.empty()) {
		entry["headers"] = headers;
	}
	if (!answer.body.empty()) {
		std::string bodyFile =
			UnusedBodyFileName(BodyFileName(answer.url, contentType, answer.method), usedBodyFiles);
		WriteFile(dir + "/" + bodyFile, answer.body);
		entry["body_file"] = bodyFile;
	}
	kept.push_back(entry);
	WriteFile(mappingFile, kept.dump(2) + "\n");
}

}  // namespace steamworks
