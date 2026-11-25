/**
 * Copyright 2026 davenonymous
 *
 * Self tests of smrunner's KeyValues natives (core/smn_keyvalues.cpp of
 * SourceMod on a KeyValues of the runner's own that behaves like Valve's):
 * types and conversions, the traversal stack, deletion, copying, the text
 * format in strings and files, escape sequences, symbols and handles.
 */
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smrunner>

// root { "values" "1"  "first" { "x" "1" }  "middle" "v"  "second" { "y" "2" } }
KeyValues MixedTree() {
	KeyValues kv = new KeyValues("root");
	kv.ImportFromString("\"root\" { \"values\" \"1\" \"first\" { \"x\" \"1\" }"
		... " \"middle\" \"v\" \"second\" { \"y\" \"2\" } }");
	return kv;
}

void AssertSection(KeyValues kv, const char[] expected) {
	char name[64];
	Assert.True(kv.GetSectionName(name, sizeof(name)), "the section has a name");
	Assert.StrEqual(name, expected);
}

public void Test_CreateNamesTheRootAndSetsTheFirstKey() {
	KeyValues kv = new KeyValues("root", "first", "value");
	AssertSection(kv, "root");
	char value[32];
	kv.GetString("first", value, sizeof(value));
	Assert.StrEqual(value, "value");
	Assert.Equal(kv.NodesInStack(), 0);
	delete kv;
}

public void Test_NewRootIsAnEmptyStringUntilItGetsKeys() {
	KeyValues kv = new KeyValues("root");
	Assert.Equal(kv.GetDataType(""), KvData_String, "Valve's constructor sets the root's value");
	kv.SetNum("child", 1);
	Assert.Equal(kv.GetDataType(""), KvData_None, "a key with subkeys is a section");
	delete kv;
}

public void Test_EveryTypeIsSetAndRead() {
	KeyValues kv = new KeyValues("root");
	kv.SetString("string", "text");
	kv.SetNum("int", -7);
	kv.SetFloat("float", 2.5);
	int wide[2] = {2, 1};
	kv.SetUInt64("uint64", wide);
	kv.SetColor("color", 10, 20, 30, 40);
	float vector[3] = {1.0, -2.5, 3.25};
	kv.SetVector("vector", vector);

	char text[32];
	kv.GetString("string", text, sizeof(text));
	Assert.StrEqual(text, "text");
	Assert.Equal(kv.GetNum("int"), -7);
	Assert.FloatEqual(kv.GetFloat("float"), 2.5);
	int wideRead[2];
	kv.GetUInt64("uint64", wideRead);
	Assert.Equal(wideRead[0], 2);
	Assert.Equal(wideRead[1], 1);
	int r, g, b, a;
	kv.GetColor("color", r, g, b, a);
	Assert.True(r == 10 && g == 20 && b == 30 && a == 40, "color is %d %d %d %d", r, g, b, a);
	float vectorRead[3];
	kv.GetVector("vector", vectorRead);
	Assert.FloatEqual(vectorRead[0], 1.0);
	Assert.FloatEqual(vectorRead[1], -2.5);
	Assert.FloatEqual(vectorRead[2], 3.25);

	Assert.Equal(kv.GetDataType("string"), KvData_String);
	Assert.Equal(kv.GetDataType("int"), KvData_Int);
	Assert.Equal(kv.GetDataType("float"), KvData_Float);
	Assert.Equal(kv.GetDataType("uint64"), KvData_UInt64);
	Assert.Equal(kv.GetDataType("color"), KvData_Color);
	Assert.Equal(kv.GetDataType("vector"), KvData_String, "vectors are strings");
	Assert.Equal(kv.GetDataType("missing"), KvData_None);
	delete kv;
}

public void Test_MissingKeysGiveTheDefaults() {
	KeyValues kv = new KeyValues("root");
	char text[32];
	kv.GetString("missing", text, sizeof(text), "fallback");
	Assert.StrEqual(text, "fallback");
	Assert.Equal(kv.GetNum("missing", 5), 5);
	Assert.FloatEqual(kv.GetFloat("missing", 1.5), 1.5);
	int wide[2];
	int wideDefault[2] = {-1, 99};
	kv.GetUInt64("missing", wide, wideDefault);
	Assert.Equal(wide[0], -1, "the low cell of the default");
	Assert.Equal(wide[1], -1, "SourceMod reads only the default's first cell, sign extended");
	int r = 1, g = 1, b = 1, a = 1;
	kv.GetColor("missing", r, g, b, a);
	Assert.True(r == 0 && g == 0 && b == 0 && a == 0, "a missing color is 0 0 0 0");
	float vector[3];
	float vectorDefault[3] = {4.0, 5.0, 6.0};
	kv.GetVector("missing", vector, vectorDefault);
	Assert.FloatEqual(vector[2], 6.0);
	delete kv;
}

public void Test_GetStringFormatsNumbersAndMakesThemStrings() {
	KeyValues kv = new KeyValues("root");
	kv.SetNum("int", 42);
	kv.SetFloat("float", 1.5);
	int wide[2] = {2, 1};
	kv.SetUInt64("uint64", wide);
	char text[32];
	kv.GetString("int", text, sizeof(text));
	Assert.StrEqual(text, "42");
	Assert.Equal(kv.GetDataType("int"), KvData_String, "reading a number as a string converts the key");
	kv.GetString("float", text, sizeof(text));
	Assert.StrEqual(text, "1.500000");
	kv.GetString("uint64", text, sizeof(text));
	Assert.StrEqual(text, "4294967298");
	delete kv;
}

public void Test_NumbersAreParsedFromStrings() {
	KeyValues kv = new KeyValues("root");
	kv.SetString("prefix", "12abc");
	kv.SetString("decimal", "3.75");
	kv.SetString("hex", "0x10");
	kv.SetString("rgba", "1 2 3 4");
	kv.SetFloat("float", 3.9);
	kv.SetNum("int", 300);
	Assert.Equal(kv.GetNum("prefix"), 12, "atoi");
	Assert.FloatEqual(kv.GetFloat("decimal"), 3.75);
	Assert.Equal(kv.GetNum("decimal"), 3);
	Assert.Equal(kv.GetNum("float"), 3, "floats are truncated");
	int wide[2];
	kv.GetUInt64("hex", wide);
	Assert.Equal(wide[0], 16);
	int r, g, b, a;
	kv.GetColor("rgba", r, g, b, a);
	Assert.True(r == 1 && g == 2 && b == 3 && a == 4, "color from a string is %d %d %d %d", r, g, b, a);
	kv.GetColor("int", r, g, b, a);
	Assert.True(r == 44 && g == 0, "an int is the red byte, got %d %d", r, g);
	delete kv;
}

public void Test_SectionReadsAsItsCellNotTheDefault() {
	KeyValues kv = new KeyValues("root");
	kv.SetNum("key", 9);
	kv.SetNum("key/child", 1);
	Assert.Equal(kv.GetDataType("key"), KvData_None);
	Assert.Equal(kv.GetNum("key", 5), 9, "Valve's union keeps the int a section once had");
	delete kv;
}

public void Test_KeyNamesIgnoreCaseAndKeepTheirFirstSpelling() {
	KeyValues kv = new KeyValues("root");
	kv.SetNum("Speed", 5);
	Assert.Equal(kv.GetNum("SPEED"), 5);
	kv.SetNum("speed", 6);
	Assert.Equal(kv.GetNum("Speed"), 6, "the same key");
	Assert.True(kv.JumpToKey("sPeEd"));
	AssertSection(kv, "Speed");
	delete kv;
}

public void Test_PathsReachAndCreateNestedKeys() {
	KeyValues kv = new KeyValues("root");
	Assert.False(kv.JumpToKey("a/b"), "no such path");
	Assert.True(kv.JumpToKey("a/b", true));
	Assert.Equal(kv.NodesInStack(), 1, "a path is one jump");
	AssertSection(kv, "b");
	kv.SetNum("c", 3);
	kv.Rewind();
	Assert.Equal(kv.GetNum("a/b/c"), 3);
	Assert.True(kv.JumpToKey(""), "an empty name is the current key");
	Assert.Equal(kv.NodesInStack(), 1);
	delete kv;
}

public void Test_TraversalVisitsSectionsOrAllKeys() {
	KeyValues kv = MixedTree();
	Assert.True(kv.GotoFirstSubKey());
	AssertSection(kv, "first");
	Assert.True(kv.GotoNextKey());
	AssertSection(kv, "second");
	Assert.False(kv.GotoNextKey(), "no section after the second");
	Assert.True(kv.GoBack());
	Assert.True(kv.GotoFirstSubKey(false));
	AssertSection(kv, "values");
	Assert.True(kv.GotoNextKey(false));
	AssertSection(kv, "first");
	Assert.True(kv.GotoNextKey(false));
	AssertSection(kv, "middle");
	Assert.Equal(kv.NodesInStack(), 1, "next keys replace the current one");
	delete kv;
}

public void Test_GoBackAndRewind() {
	KeyValues kv = MixedTree();
	Assert.False(kv.GoBack(), "nothing above the root");
	kv.JumpToKey("first");
	kv.Rewind(false);
	Assert.Equal(kv.NodesInStack(), 2, "the root is pushed onto the history");
	AssertSection(kv, "root");
	Assert.True(kv.GoBack());
	AssertSection(kv, "first");
	kv.Rewind();
	Assert.Equal(kv.NodesInStack(), 0);
	AssertSection(kv, "root");
	delete kv;
}

public void Test_SavePositionPushesTheCurrentKey() {
	KeyValues kv = MixedTree();
	Assert.False(kv.SavePosition(), "not at the root");
	kv.JumpToKey("first");
	Assert.True(kv.SavePosition());
	Assert.Equal(kv.NodesInStack(), 2);
	Assert.True(kv.GotoNextKey());
	AssertSection(kv, "second");
	kv.GoBack();
	AssertSection(kv, "first");
	delete kv;
}

public void Test_DeleteThisDuringTraversal() {
	KeyValues kv = new KeyValues("root");
	char names[][] = {"a", "b", "c", "d"};
	for (int i = 0; i < sizeof(names); i++) {
		kv.JumpToKey(names[i], true);
		kv.SetNum("drop", i % 2);
		kv.GoBack();
	}
	char results[32];
	Assert.True(kv.GotoFirstSubKey());
	for (;;) {
		if (kv.GetNum("drop")) {
			int result = kv.DeleteThis();
			char part[8];
			Format(part, sizeof(part), "%d ", result);
			StrCat(results, sizeof(results), part);
			if (result == 1) {
				continue;
			}
			break;
		}
		if (!kv.GotoNextKey()) {
			kv.GoBack();
			break;
		}
	}
	Assert.StrEqual(results, "1 -1 ", "1 moves to the next key, -1 back to the parent");
	Assert.Equal(kv.NodesInStack(), 0);
	Assert.True(kv.JumpToKey("a") && kv.GoBack());
	Assert.False(kv.JumpToKey("b"), "b was deleted");
	Assert.True(kv.JumpToKey("c") && kv.GoBack());
	Assert.False(kv.JumpToKey("d"), "d was deleted");
	delete kv;
}

public void Test_DeleteThisNeedsAParent() {
	KeyValues kv = MixedTree();
	Assert.Equal(kv.DeleteThis(), 0, "the root is not deleted");
	kv.JumpToKey("first");
	kv.SavePosition();
	Assert.Equal(kv.DeleteThis(), 0, "a saved position is not a subkey of itself");
	Assert.Equal(kv.NodesInStack(), 2);
	delete kv;
}

public void Test_DeleteKeyWorksBelowTheRootOnly() {
	KeyValues kv = MixedTree();
	Assert.False(kv.DeleteKey("values"), "SourceMod refuses at the root");
	kv.JumpToKey("first");
	Assert.True(kv.DeleteKey("x"));
	Assert.False(kv.DeleteKey("x"), "already gone");
	kv.Rewind();
	Assert.Equal(kv.GetNum("first/x", -1), -1);
	delete kv;
}

public void Test_DeletingAKeyTheStackHoldsFails() {
	Runner.ExpectError("while the traversal stack still holds it");
	KeyValues kv = new KeyValues("root");
	kv.JumpToKey("a/b", true);
	kv.Rewind(false);
	kv.JumpToKey("a");
	kv.DeleteThis();
}

public void Test_DeleteKeyOfANestedPathFails() {
	Runner.ExpectError("names a key below a subkey");
	KeyValues kv = new KeyValues("root");
	kv.SetNum("a/b/c", 1);
	kv.JumpToKey("a");
	kv.DeleteKey("b/c");
}

public void Test_ImportReplacesTheSubkeysWithCopies() {
	KeyValues origin = new KeyValues("origin");
	origin.SetNum("a", 1);
	origin.SetNum("s/b", 2);
	KeyValues destination = new KeyValues("destination");
	destination.SetString("old", "x");
	destination.Import(origin);
	Assert.Equal(destination.GetNum("a"), 1);
	Assert.Equal(destination.GetNum("s/b"), 2);
	Assert.Equal(destination.GetNum("old", -1), -1, "the old subkeys are replaced");
	origin.SetNum("s/b", 3);
	Assert.Equal(destination.GetNum("s/b"), 2, "a deep copy");
	AssertSection(destination, "destination");

	KeyValues empty = new KeyValues("empty");
	KvCopySubkeys(empty, destination);
	Assert.Equal(destination.GetNum("a"), 1, "an origin without subkeys changes nothing");
	delete origin;
	delete destination;
	delete empty;
}

public void Test_ExportToStringWritesTheTextFormat() {
	KeyValues kv = new KeyValues("root");
	kv.SetString("name", "value");
	kv.SetNum("count", 3);
	kv.SetFloat("ratio", 0.5);
	int wide[2] = {2, 1};
	kv.SetUInt64("id", wide);
	kv.SetString("section/inner", "x");
	kv.SetString("empty", "");
	kv.JumpToKey("nothing", true);
	kv.Rewind();
	char text[256];
	kv.ExportToString(text, sizeof(text));
	Assert.StrEqual(text,
		"\"root\"\n{\n\t\"name\"\t\t\"value\"\n\t\"count\"\t\t\"3\"\n\t\"ratio\"\t\t\"0.500000\"\n"
		... "\t\"id\"\t\t\"0x0000000100000002\"\n\t\"section\"\n\t{\n\t\t\"inner\"\t\t\"x\"\n\t}\n}\n",
		"empty strings and empty sections are left out");
	delete kv;
}

public void Test_ExportToStringNeedsABuffer() {
	Runner.ExpectError("ExportToString needs a buffer of at least 1 byte, not 0");
	KeyValues kv = new KeyValues("r");
	char text[8];
	kv.ExportToString(text, 0);
}

public void Test_ExportLengthAndTruncation() {
	KeyValues kv = new KeyValues("r");
	kv.SetString("k", "v");
	Assert.Equal(kv.ExportLength, 18);
	char text[8];
	Assert.Equal(kv.ExportToString(text, sizeof(text)), 18, "the whole length");
	Assert.StrEqual(text, "\"r\"\n{\n\t");
	delete kv;
}

public void Test_ImportFromStringParsesTokensAndTypes() {
	KeyValues kv = new KeyValues("root");
	Assert.True(kv.ImportFromString(
		"\"root\"\n{\n\t// a comment\n\t\"int\" \"42\"\n\t\"float\" \"1.5\"\n\t\"text\" \"hello world\"\n"
		... "\tunquoted value\n\t\"wide\" \"0x0000000100000002\"\n\t\"dup\" \"1\"\n\t\"dup\" \"2\"\n"
		... "\tsub { k v }\n}\n"));
	Assert.Equal(kv.GetDataType("int"), KvData_Int);
	Assert.Equal(kv.GetDataType("float"), KvData_Float);
	Assert.Equal(kv.GetDataType("text"), KvData_String);
	Assert.Equal(kv.GetDataType("wide"), KvData_UInt64);
	char text[32];
	kv.GetString("unquoted", text, sizeof(text));
	Assert.StrEqual(text, "value");
	kv.GetString("sub/k", text, sizeof(text));
	Assert.StrEqual(text, "v");
	Assert.Equal(kv.GetNum("dup"), 1, "the first of duplicate keys");
	int dups;
	kv.GotoFirstSubKey(false);
	do {
		kv.GetSectionName(text, sizeof(text));
		dups += StrEqual(text, "dup") ? 1 : 0;
	} while (kv.GotoNextKey(false));
	Assert.Equal(dups, 2, "duplicate keys are kept");
	delete kv;
}

public void Test_ConditionalsAreThoseOfALinuxServer() {
	KeyValues kv = new KeyValues("root");
	kv.ImportFromString(
		"\"root\" { \"win32\" \"1\" [$WIN32] \"x360\" \"1\" [$X360] \"notx360\" \"1\" [!$X360]"
		... " \"linux\" [$LINUX] { \"k\" \"1\" } \"osx\" [$OSX] { \"k\" \"1\" } }");
	Assert.Equal(kv.GetNum("win32"), 1, "$WIN32 means any PC");
	Assert.Equal(kv.GetNum("x360", -1), -1);
	Assert.Equal(kv.GetNum("notx360"), 1);
	Assert.Equal(kv.GetNum("linux/k"), 1);
	Assert.Equal(kv.GetNum("osx/k", -1), -1);
	delete kv;
}

public void Test_ImportFromStringRenamesAndAppends() {
	KeyValues kv = new KeyValues("root");
	kv.SetNum("kept", 1);
	kv.ImportFromString("\"loaded\" { \"new\" \"2\" }");
	AssertSection(kv, "loaded");
	Assert.Equal(kv.GetNum("kept"), 1);
	Assert.Equal(kv.GetNum("new"), 2);
	delete kv;
}

public void Test_ParseErrorsAreReportedOnTheConsole() {
	KeyValues kv = new KeyValues("root");
	Assert.True(kv.ImportFromString("\"root\" { \"key\" ", "broken.txt"), "Valve's parser always succeeds");
	Assert.ConsoleContains("KeyValues Error: RecursiveLoadFromBuffer:  got NULL key in file broken.txt");
	delete kv;
}

public void Test_FilesRoundTrip() {
	KeyValues kv = new KeyValues("root");
	kv.SetString("name", "value");
	kv.SetNum("nested/count", 4);
	Assert.True(kv.ExportToFile("kv_roundtrip.txt"));
	Assert.True(FileExists("kv_roundtrip.txt"), "written to the game folder");
	Assert.False(kv.ExportToFile("no/such/folder/kv.txt"), "folders are not created");

	Handle loaded = CreateKeyValues("other");
	Assert.True(FileToKeyValues(loaded, "kv_roundtrip.txt"));
	char text[32];
	KvGetSectionName(loaded, text, sizeof(text));
	Assert.StrEqual(text, "root");
	KvGetString(loaded, "name", text, sizeof(text));
	Assert.StrEqual(text, "value");
	Assert.Equal(KvGetNum(loaded, "nested/count"), 4);
	Assert.False(FileToKeyValues(loaded, "missing.txt"));
	delete kv;
	delete loaded;
}

public void Test_EscapeSequences() {
	KeyValues plain = new KeyValues("root");
	plain.ImportFromString("\"root\" { \"path\" \"c:\\dir\\new\" }");
	char text[64];
	plain.GetString("path", text, sizeof(text));
	Assert.StrEqual(text, "c:\\dir\\new", "without escape sequences a backslash is a backslash");

	KeyValues escaped = new KeyValues("root");
	escaped.SetEscapeSequences(true);
	escaped.ImportFromString("\"root\" { \"tab\" \"a\\tb\" \"quote\" \"say \\\"hi\\\"\" }");
	escaped.GetString("tab", text, sizeof(text));
	Assert.StrEqual(text, "a\tb");
	escaped.GetString("quote", text, sizeof(text));
	Assert.StrEqual(text, "say \"hi\"");

	escaped.SetString("slash", "a\\b");
	plain.SetString("slash", "a\\b");
	plain.SetString("quote", "\"");
	char exported[256];
	escaped.ExportToString(exported, sizeof(exported));
	Assert.StrContains(exported, "\"slash\"\t\t\"a\\\\b\"", "backslashes are escaped with escape sequences");
	plain.ExportToString(exported, sizeof(exported));
	Assert.StrContains(exported, "\"slash\"\t\t\"a\\b\"", "and kept without");
	Assert.StrContains(exported, "\"quote\"\t\t\"\\\"\"", "quotes are always escaped");
	delete plain;
	delete escaped;
}

public void Test_SymbolsNameKeysAcrossTrees() {
	KeyValues kv = new KeyValues("root");
	kv.SetNum("Alpha/leaf", 1);
	kv.JumpToKey("Alpha");
	int alpha;
	Assert.True(kv.GetSectionSymbol(alpha));
	Assert.NotEqual(alpha, 0);
	int leaf;
	Assert.True(kv.GetNameSymbol("LEAF", leaf));
	kv.GoBack();
	Assert.False(kv.GetNameSymbol("Alpha", alpha), "SourceMod does not answer at the root");
	char name[32];
	Assert.True(kv.FindKeyById(alpha, name, sizeof(name)));
	Assert.StrEqual(name, "Alpha");
	Assert.True(kv.JumpToKeySymbol(alpha));
	AssertSection(kv, "Alpha");

	KeyValues other = new KeyValues("other");
	other.SetNum("leaf", 2);
	Assert.True(other.JumpToKeySymbol(leaf), "symbols are shared by all trees");
	Assert.False(other.JumpToKeySymbol(alpha));
	delete kv;
	delete other;
}

public void Test_SetSectionNameRenamesTheCurrentKey() {
	KeyValues kv = new KeyValues("root");
	kv.SetNum("before/value", 1);
	kv.JumpToKey("before");
	kv.SetSectionName("after");
	kv.Rewind();
	Assert.Equal(kv.GetNum("after/value"), 1);
	Assert.False(kv.JumpToKey("before"));
	delete kv;
}

public void Test_ClonesShareTheTreeAndTheStack() {
	KeyValues kv = new KeyValues("root");
	KeyValues clone = view_as<KeyValues>(CloneHandle(kv));
	clone.SetNum("shared", 1);
	Assert.Equal(kv.GetNum("shared"), 1);
	clone.JumpToKey("section", true);
	Assert.Equal(kv.NodesInStack(), 1);
	delete clone;
	AssertSection(kv, "section");
	delete kv;
}

public void Test_InvalidHandleFails() {
	Runner.ExpectError("Invalid key value handle");
	KeyValues bad = view_as<KeyValues>(0xBAD);
	bad.GetNum("key");
}

public void Test_DeletedHandleFails() {
	Runner.ExpectError("Invalid key value handle");
	KeyValues kv = new KeyValues("root");
	KeyValues copy = kv;
	delete kv;
	copy.SetNum("key", 1);
}
