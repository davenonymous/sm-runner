#!/bin/sh
# Copyright 2026 davenonymous
#
# Runs smrunner's self tests and the examples with one smrunner binary
# (smrunner or smrunner64), in the folder the build lays out:
#   tests/, examples/       the compiled plugins, as in the source tree
#   sources/tests/          the self tests' sources (the breakpoint tests
#                           read target.sp, the SteamWorks tests their
#                           mapping file)
#   sources/examples/       the examples' sources (mapping files)
#   deps/smjansson/         SMJansson's extensions/ folder
# The SteamWorks suites start a local HTTP server (python3,
# tests/http_server.py) on a free port it picks. Every temporary folder is
# below one, which goes with the HTTP server on every way out.
# Every suite must pass, except tests/selftest/expected_failures, whose
# tests must all fail, each with its own message.
#
#   sh run.sh /opt/smrunner/bin/smrunner
set -eu
runner="$1"
cd "$(dirname "$0")"
work="$(mktemp -d)"
server=""
cleanup() {
	if [ -n "${server}" ]; then
		kill "${server}" 2> /dev/null || true
	fi
	rm -rf "${work}"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
echo "=== $(basename "${runner}")"

suite() {
	echo "--- $*"
	"${runner}" test --timeout 60 "$@"
}

selftest=tests/selftest
suite "${selftest}/test_basics.smx" --plugin "${selftest}/target.smx"
suite "${selftest}/test_hooks.smx" --plugin "${selftest}/target.smx" \
	--plugin "${selftest}/hook_provider.smx" --plugin "${selftest}/hook_dependent.smx"
suite "${selftest}/test_breakpoints.smx" --plugin "${selftest}/target.smx" \
	--file "sources/${selftest}/target.sp=addons/sourcemod/data/target.sp"
suite "${selftest}/test_keyvalues.smx"
suite "${selftest}/test_menus.smx" --plugin "${selftest}/menu_target.smx"
# A drawn menu: the box as it is with --color never, with escape codes only
# with --color always.
echo "--- drawn menus"
escape="$(printf '\033[')"
plain="$("${runner}" test "${selftest}/test_menus.smx" --plugin "${selftest}/menu_target.smx" --filter Printed --color never)"
for expected in "╭─ Pick a fruit ───────╮" "│ 3. Cherry (disabled) │" "│ 8. Previous    │" "╰─────────────── 20 s ─╯"; do
	if ! echo "${plain}" | grep -qF -- "${expected}"; then
		echo "drawn menus: missing \"${expected}\"" >&2
		exit 1
	fi
done
if echo "${plain}" | grep -qF -- "${escape}"; then
	echo "drawn menus: escape codes with --color never" >&2
	exit 1
fi
colored="$("${runner}" test "${selftest}/test_menus.smx" --plugin "${selftest}/menu_target.smx" --filter Printed --color always)"
if ! echo "${colored}" | grep -qF -- "${escape}36m8. Previous"; then
	echo "drawn menus: no colors with --color always" >&2
	exit 1
fi
suite "${selftest}/test_usermessages.smx"
suite "${selftest}/test_cvars.smx" \
	--cvar smrunner_test_limit=42 --cvar smrunner_test_greeting=hi --cvar sm_maphistory_size=3
suite "${selftest}/test_topmenus.smx" --plugin adminmenu

# The SteamWorks stand-in: with the mapping file of sources/, and without one.
suite "${selftest}/test_steamworks.smx" --plugin "${selftest}/steamworks_target.smx" \
	--steamworks "sources/${selftest}/steamworks"
suite "${selftest}/test_steamworks_bare.smx" --plugin "${selftest}/steamworks_target.smx" --steamworks
# Passthrough and record mode against a local HTTP server; what was recorded
# then answers with the server gone.
echo "--- SteamWorks passthrough and record mode"
web="${work}/web"
record="${work}/record"
mkdir "${web}" "${record}"
printf '{"real": true}' > "${web}/hello.json"
printf 'other' > "${web}/other.txt"
printf '[1, 2]' > "${web}/items.json"
printf '{"get": true}' > "${web}/items.post.json"
printf 'plain data' > "${web}/data"
printf 'binary data' > "${web}/data.bin"
python3 sources/tests/http_server.py "${web}" "${work}/port" > /dev/null 2>&1 &
server=$!
for attempt in $(seq 50); do
	[ -s "${work}/port" ] && break
	sleep 0.1
done
if [ ! -s "${work}/port" ]; then
	echo "the local HTTP server did not start" >&2
	exit 1
fi
port="$(cat "${work}/port")"
base="http://127.0.0.1:${port}"
suite "${selftest}/test_steamworks_passthrough.smx" --plugin "${selftest}/steamworks_target.smx" \
	--steamworks --steamworks-passthrough 127.0.0.1 --steamworks-record "${record}" --cvar "smrunner_http_base=${base}"
kill "${server}"
wait "${server}" 2> /dev/null || true
server=""
for expected in "\"url\": \"${base}/hello.json\"" "\"body_file\": \"bodies/127.0.0.1/hello.json\"" \
	"\"url\": \"http://localhost:${port}/other.txt\"" "\"url\": \"${base}/moved\""; do
	if ! grep -qF -- "${expected}" "${record}/steamworks.json"; then
		echo "record mode: steamworks.json lacks ${expected}" >&2
		cat "${record}/steamworks.json" >&2
		exit 1
	fi
done
if [ "$(cat "${record}/bodies/127.0.0.1/hello.json")" != '{"real": true}' ]; then
	echo "record mode: the body file of hello.json differs" >&2
	exit 1
fi
# A GET and a POST to one path have a body file each, also beside a GET of
# a path that looks like the POST's; two answers whose names would be the
# same (data as application/octet-stream and data.bin) get one each; a HEAD
# answer keeps its Content-Length.
if [ "$(cat "${record}/bodies/127.0.0.1/items.json")" != '[1, 2]' ] ||
	[ "$(cat "${record}/bodies/127.0.0.1/items@post.json")" != '{"created": true}' ] ||
	[ "$(cat "${record}/bodies/127.0.0.1/items.post.json")" != '{"get": true}' ]; then
	echo "record mode: the requests to items.json and items.post.json do not have a body file each" >&2
	ls -R "${record}/bodies" >&2
	exit 1
fi
if ! python3 -I -c '
import json, os, sys
folder = sys.argv[1]
entries = json.load(open(os.path.join(folder, "steamworks.json")))
files = [e["body_file"] for e in entries if "body_file" in e]
data = {e["url"].rsplit("/", 1)[1]: e["body_file"] for e in entries if e["url"].rsplit("/", 1)[1] in ("data", "data.bin")}
bodies = {name: open(os.path.join(folder, path)).read() for name, path in data.items()}
ok = len(files) == len(set(files)) and sorted(data.values()) == ["bodies/127.0.0.1/data.bin", "bodies/127.0.0.1/data~2.bin"]
sys.exit(0 if ok and bodies == {"data": "plain data", "data.bin": "binary data"} else 1)
' "${record}"; then
	echo "record mode: data and data.bin do not have a body file each" >&2
	cat "${record}/steamworks.json" >&2
	exit 1
fi
if ! python3 -I -c '
import json, sys
entries = json.load(open(sys.argv[1]))
head = [e for e in entries if e.get("method") == "HEAD" and e["url"].endswith("/hello.json")]
sys.exit(0 if len(head) == 1 and head[0].get("headers", {}).get("Content-Length") == "14" else 1)
' "${record}/steamworks.json"; then
	echo "record mode: the HEAD of hello.json was not recorded with its Content-Length" >&2
	cat "${record}/steamworks.json" >&2
	exit 1
fi
if grep -qF -- "/elsewhere" "${record}/steamworks.json"; then
	echo "record mode: the redirect to a host not allowed was recorded" >&2
	exit 1
fi
suite "${selftest}/test_steamworks_replay.smx" --plugin "${selftest}/steamworks_target.smx" \
	--steamworks "${record}" --cvar "smrunner_http_base=${base}"
# A plugin that requires SteamWorks does not run without --steamworks, and a
# wrong mapping file fails the run, naming the entry.
echo "--- SteamWorks run mode"
if "${runner}" run "${selftest}/steamworks_target.smx" --frames 2 --quiet 2> /dev/null; then
	echo "steamworks_target ran without --steamworks" >&2
	exit 1
fi
broken="${work}/broken"
mkdir "${broken}"
echo '[{"url": "http://api.test/", "fail": "maybe"}]' > "${broken}/steamworks.json"
if output="$("${runner}" run "${selftest}/steamworks_target.smx" --steamworks "${broken}" --frames 2 2>&1)"; then
	echo "a wrong mapping file did not fail the run" >&2
	exit 1
fi
if ! echo "${output}" | grep -qF 'steamworks.json: entry 1 "fail" is neither "io" nor "timeout"'; then
	echo "a wrong mapping file was not named: ${output}" >&2
	exit 1
fi

# The must-fail suite: smrunner exits 1 and names every failure.
echo "--- ${selftest}/expected_failures.smx (every test must fail)"
output="$("${runner}" test --timeout 3 "${selftest}/expected_failures.smx" --plugin "${selftest}/target.smx" || true)"
echo "${output}" | grep -v '^          |'
for expected in \
	"FAIL    Test_AssertEqualFails" "Assert.Equal failed: expected 5, got 4: arithmetic still works (expected_failures.sp:" \
	"FAIL    Test_AssertStrEqualFails" "Assert.StrEqual failed: expected \"expected\", got \"actual\"" \
	"FAIL    Test_AssertFloatEqualFails" "Assert.FloatEqual failed: expected 1.5, got 1 (tolerance 0.1)" \
	"FAIL    Test_ConsoleContainsFails" "the console did not print \"this text was never printed\"" \
	"FAIL    Test_UnexpectedPluginErrorFails" "plugin error: target.smx: Divide by zero" \
	"FAIL    Test_UnmetExpectedErrorFails" "expected an error containing \"an error that never happens\", none occurred" \
	"FAIL    Test_ErrorInTheTestPluginFails" "Array index out-of-bounds" \
	"FAIL    Test_AssertionInACallbackFails" "Assert.Fail: failed in a frame callback" \
	"FAIL    Test_UnknownBreakpointLineFails" "no plugin has a statement at or after nosuchfile.sp:3" \
	"FAIL    Test_UnknownBreakpointFunctionFails" "no plugin has a function NoSuchFunction" \
	"TIMEOUT Test_TimeoutIsReported" "the test did not end within 3 seconds" \
	"FAIL    Test_LongAssertionFailsOnce" \
	"FAIL    Test_AssertClientMenuWithoutAMenuFails" "Assert.ClientMenu failed: client 1 shows no menu" \
	"FAIL    Test_SelectMenuItemWithoutAMatchFails" "Runner.SelectMenuItem failed: no line client 1 can select contains \"pear\"" \
	"FAIL    Test_SelectMenuItemWithSeveralMatchesFails" "Runner.SelectMenuItem failed: more than one line contains \"apple\": \"->1. Red apple\", \"->2. Green apple\"" \
	"FAIL    Test_AssertClientReceivedOnMissingChatFails" "Assert.ClientReceived failed: client 1 did not receive \"goodbye\"" \
	"16 tests: 0 passed, 16 failed, 0 skipped"; do
	if ! echo "${output}" | grep -qF -- "${expected}"; then
		echo "expected_failures: missing \"${expected}\"" >&2
		exit 1
	fi
done
if echo "${output}" | grep -qF "plugin error: expected_failures.smx: Assert.StrEqual"; then
	echo "expected_failures: a long assertion was also reported as a plugin error" >&2
	exit 1
fi

# run mode: a clean run passes, a plugin that fails in OnPluginStart fails it.
echo "--- run mode"
"${runner}" run examples/json-status/json-status.smx --extensions deps/smjansson \
	--command "sm_status_json" --frames 10 --quiet
if "${runner}" run "${selftest}/broken_start.smx" --frames 2 --quiet 2>/dev/null; then
	echo "run mode passed a plugin that failed to start" >&2
	exit 1
fi
# A plugin that fails in AskPluginLoad2 is asked once, its failure is
# logged once and fails the run.
if output="$("${runner}" run "${selftest}/failing_load.smx" --frames 2 2>&1)"; then
	echo "run mode passed a plugin that failed to load" >&2
	exit 1
fi
if [ "$(echo "${output}" | grep -c "failing_load: AskPluginLoad2 ran")" != 1 ]; then
	echo "run mode: AskPluginLoad2 of a failing plugin did not run exactly once: ${output}" >&2
	exit 1
fi
logged="$(echo "${output}" | sed -n '/^smrunner: SourceMod reported errors:$/,$p')"
if [ "$(echo "${logged}" | grep -c "Failed to load plugin \"failing_load.smx\": failing on purpose")" != 1 ]; then
	echo "run mode: the failure of failing_load.smx was not logged exactly once: ${output}" >&2
	exit 1
fi
if ! echo "${output}" | grep -qF "smrunner: plugin failing_load.smx (failed to load: failing on purpose in AskPluginLoad2)"; then
	echo "run mode: the failed plugin was not named: ${output}" >&2
	exit 1
fi
# The command line: numbers are read whole, the versions come from the
# sources smrunner was built from.
if output="$("${runner}" run "${selftest}/target.smx" --tickrate 66abc 2>&1)"; then
	echo "--tickrate took 66abc" >&2
	exit 1
fi
if ! echo "${output}" | grep -qF "smrunner: --tickrate needs a number between 1 and 1000"; then
	echo "--tickrate 66abc was not refused by name: ${output}" >&2
	exit 1
fi
# A --cvar no cvar takes is named.
output="$("${runner}" run "${selftest}/target.smx" --cvar smrunner_never_made=1 --frames 2 2>&1)"
if ! echo "${output}" | grep -qF "[smrunner] warning: --cvar smrunner_never_made: no cvar of that name was made"; then
	echo "an unused --cvar was not named: ${output}" >&2
	exit 1
fi
if ! "${runner}" --version | grep -qE "^smrunner .* \(SourceMod 1\.12\.[0-9]+-[a-z]+, SourcePawn API [0-9]+\)$"; then
	echo "--version does not name SourceMod and SourcePawn: $("${runner}" --version)" >&2
	exit 1
fi
# The test API in run mode: what needs no test works, a failed assertion
# fails the run with its message, what needs a test throws.
output="$("${runner}" run "${selftest}/run_mode.smx" --command runmode_menu --frames 2 2>&1)"
if ! echo "${output}" | grep -qF "menu of client 1 (alice): none"; then
	echo "run mode: Runner.PrintClientMenu printed nothing: ${output}" >&2
	exit 1
fi
if output="$("${runner}" run "${selftest}/run_mode.smx" --command runmode_assert --frames 2 2>&1)"; then
	echo "run mode passed a failed assertion" >&2
	exit 1
fi
if ! echo "${output}" | grep -qF "smrunner: Assert.Equal failed: expected 0, got 25: asserted in run mode (run_mode.sp:"; then
	echo "run mode: the failed assertion was not named: ${output}" >&2
	exit 1
fi
if output="$("${runner}" run "${selftest}/run_mode.smx" --command runmode_hook --frames 2 2>&1)"; then
	echo "run mode passed Runner.HookNative" >&2
	exit 1
fi
if ! echo "${output}" | grep -qF "Runner.HookNative needs a test: it works in smrunner test, not in smrunner run"; then
	echo "run mode: Runner.HookNative did not say it needs a test: ${output}" >&2
	exit 1
fi

# Sandboxes: a temporary server folder goes after its run or test, one kept
# with --keep-sandbox stays and is named, --sandbox DIR has a folder per test.
echo "--- sandboxes"
scratch="${work}/sandboxes"
mkdir "${scratch}"
TMPDIR="${scratch}" "${runner}" run "${selftest}/target.smx" --frames 2 --quiet
TMPDIR="${scratch}" "${runner}" test "${selftest}/test_basics.smx" --plugin "${selftest}/target.smx" \
	--filter Test_ServerIsUp > /dev/null
if [ -n "$(ls -A "${scratch}")" ]; then
	echo "sandboxes: a temporary server folder was left in ${scratch}" >&2
	exit 1
fi
kept="$(TMPDIR="${scratch}" "${runner}" test "${selftest}/test_basics.smx" --plugin "${selftest}/target.smx" \
	--filter Test_ServerIsUp --keep-sandbox 2>&1 > /dev/null)"
if ! echo "${kept}" | grep -qF "the server folder is kept in ${scratch}/smrunner." ||
	[ ! -d "$(ls -d "${scratch}"/smrunner.*)/tf/addons/sourcemod" ]; then
	echo "sandboxes: --keep-sandbox did not keep the folder: ${kept}" >&2
	exit 1
fi
"${runner}" test "${selftest}/test_basics.smx" --plugin "${selftest}/target.smx" --filter Test_ServerIsUp \
	--sandbox "${scratch}/given" --quiet > /dev/null
if [ ! -d "${scratch}/given/test_basics.smx/Test_ServerIsUpWithConfigsExecuted/tf/addons/sourcemod" ]; then
	echo "sandboxes: --sandbox has no folder for the test" >&2
	exit 1
fi

suite examples/json-status/test_json_status.smx --plugin examples/json-status/json-status.smx \
	--extensions deps/smjansson
suite examples/menu-vote/test_menu_vote.smx --plugin examples/menu-vote/menu-vote.smx \
	--maps cp_process_final,koth_viaduct
suite examples/team-admin/test_team_admin.smx --plugin examples/team-admin/team-admin.smx --plugin adminmenu
suite examples/steamworks-motd/test_steamworks_motd.smx --plugin examples/steamworks-motd/steamworks-motd.smx \
	--steamworks sources/examples/steamworks-motd/steamworks
echo "=== $(basename "${runner}"): all suites passed"
