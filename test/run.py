#!/usr/bin/env python3
# Copyright 2026 davenonymous
"""The test action's driver: plans a test run, then compiles and runs it.

    run.py plan        reads the action's inputs (INPUT_* variables), makes
                       the results folder, writes the plan to a JSON file
                       and tells the action whether it has to install smrunner
    run.py run PLAN    compiles the .sp files with $SPCOMP, runs `smrunner
                       test` with every runner of the plan, writes the job
                       summary, annotations and the step outputs

`run` exits 0 when the tests ran, whatever their result; the action fails
the step afterwards from the `result` output, so the results can be uploaded
first. Only Python's standard library is used.
"""

import glob
import html
import json
import os
import re
import secrets
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

RUNNERS = {
	"smrunner": ["smrunner"],
	"smrunner64": ["smrunner64"],
	"both": ["smrunner", "smrunner64"],
}
PLUGIN_SUFFIXES = (".sp", ".smx")
# "... (test_greeter.sp:13)" at the end of a failure message.
LOCATION = re.compile(r"\(([^()\s/]+\.(?:sp|inc)):(\d+)\)$")


class ActionError(Exception):
	"""A problem with the inputs or the setup, reported as one error annotation."""


# --- GitHub Actions workflow commands and files ---------------------------


def escape_data(text):
	return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def escape_property(text):
	return escape_data(text).replace(":", "%3A").replace(",", "%2C")


def annotate(level, message, title, file=None, line=None):
	properties = [f"title={escape_property(title)}"]
	if file:
		properties.append(f"file={escape_property(file)}")
	if line:
		properties.append(f"line={line}")
	print(f"::{level} {','.join(properties)}::{escape_data(message)}", flush=True)


def write_outputs(outputs):
	with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
		for name, value in outputs.items():
			delimiter = f"SMRUNNER_{secrets.token_hex(8)}"
			stream.write(f"{name}<<{delimiter}\n{value}\n{delimiter}\n")


def append_summary(markdown):
	path = os.environ.get("GITHUB_STEP_SUMMARY")
	if not path:
		return
	with open(path, "a", encoding="utf-8") as stream:
		stream.write(markdown)


# --- plan: the inputs, parsed once -----------------------------------------


def input_text(name):
	return os.environ.get("INPUT_" + name.upper().replace("-", "_"), "").strip()


def input_lines(name):
	return [line.strip() for line in input_text(name).splitlines() if line.strip()]


def input_flag(name):
	value = input_text(name)
	if value not in ("true", "false"):
		raise ActionError(f"{name} must be true or false, not \"{value}\"")
	return value == "true"


def expand(pattern):
	"""The files a line of tests or plugins names, in sorted order."""
	return sorted(path for path in glob.glob(pattern, recursive=True) if os.path.isfile(path))


def check_plugin_file(path, name):
	if not path.endswith(PLUGIN_SUFFIXES):
		raise ActionError(f"{name}: {path} is neither a .sp nor a .smx file")


def unique(items):
	return list(dict.fromkeys(items))


def parse_tests():
	tests = []
	for pattern in input_lines("tests"):
		matches = expand(pattern)
		if not matches:
			annotate("warning", f"tests: {pattern} matches no file", "smrunner test")
		for path in matches:
			check_plugin_file(path, "tests")
		tests += matches
	if not tests:
		raise ActionError("tests: no test plugin found; check the patterns and working-directory")
	return unique(tests)


def parse_plugins():
	"""Files in the given order; a line that matches no file goes to smrunner as it is (a stock plugin)."""
	plugins = []
	for pattern in input_lines("plugins"):
		matches = expand(pattern) or [pattern]
		for path in matches:
			if os.path.isfile(path):
				check_plugin_file(path, "plugins")
		plugins += matches
	return unique(plugins)


def parse_timeout():
	timeout = input_text("timeout")
	if timeout and not timeout.isdigit():
		raise ActionError(f"timeout must be a number of seconds, not \"{timeout}\"")
	return timeout


def parse_steamworks():
	steamworks = input_text("steamworks")
	hosts = input_lines("steamworks-passthrough")
	if hosts and not steamworks:
		raise ActionError("steamworks-passthrough needs steamworks")
	if not steamworks:
		return []
	options = ["--steamworks"] if steamworks == "true" else [f"--steamworks={steamworks}"]
	for host in hosts:
		options += ["--steamworks-passthrough", host]
	return options


def server_options():
	"""The smrunner options of the inputs, without test plugins, plugins and --junit."""
	options = []
	for name, flag in (("extensions", "--extensions"), ("extension", "--extension"),
			("cvars", "--cvar"), ("files", "--file"), ("core", "--core")):
		for value in input_lines(name):
			options += [flag, value]
	if input_text("filter"):
		options += ["--filter", input_text("filter")]
	timeout = parse_timeout()
	if timeout:
		options += ["--timeout", timeout]
	return options + parse_steamworks() + input_lines("args")


def artifact_name():
	given = input_text("artifact-name")
	if given:
		return given
	return f"smrunner-results-{os.environ.get('GITHUB_JOB', 'job')}-{secrets.token_hex(4)}"


def plan():
	runner = input_text("runner")
	if runner not in RUNNERS:
		raise ActionError(f"runner must be smrunner, smrunner64 or both, not \"{runner}\"")
	working_directory = input_text("working-directory") or "."
	if not os.path.isdir(working_directory):
		raise ActionError(f"working-directory {working_directory} is not a folder")
	os.chdir(working_directory)

	tests = parse_tests()
	plugins = parse_plugins()
	sources = unique(path for path in tests + plugins if path.endswith(".sp") and os.path.isfile(path))
	run_id = secrets.token_hex(4)
	results = os.path.join(os.environ["RUNNER_TEMP"], f"smrunner-results-{run_id}")
	os.makedirs(results)
	the_plan = {
		"working_directory": os.getcwd(),
		"results": results,
		"runners": RUNNERS[runner],
		"tests": tests,
		"plugins": plugins,
		"sources": sources,
		"include_paths": [os.path.abspath(path) for path in input_lines("include-paths")],
		"options": server_options(),
		"annotations": input_flag("annotations"),
	}
	plan_file = os.path.join(os.environ["RUNNER_TEMP"], f"smrunner-plan-{run_id}.json")
	with open(plan_file, "w", encoding="utf-8") as stream:
		json.dump(the_plan, stream, indent=1)
	write_outputs({
		"plan": plan_file,
		"results": results,
		"artifact-name": artifact_name(),
		"needs-setup": str(not os.environ.get("SMRUNNER_HOME")).lower(),
		"needs-spcomp": str(bool(sources)).lower(),
		"needs-32bit-runtime": str("smrunner" in the_plan["runners"]).lower(),
	})


# --- run: compile, test, report --------------------------------------------


def compiled_path(results, source):
	relative = os.path.relpath(os.path.abspath(source))
	if relative.startswith(".."):
		relative = os.path.abspath(source).lstrip("/")
	return os.path.join(results, "smx", os.path.splitext(relative)[0] + ".smx")


def include_paths(the_plan, source):
	own = os.path.join(os.path.dirname(os.path.abspath(source)), "include")
	paths = the_plan["include_paths"] + ([own] if os.path.isdir(own) else [])
	return paths + [os.environ["SMRUNNER_INCLUDE"], os.environ["SOURCEMOD_INCLUDE"]]


def compile_sources(the_plan):
	"""Compiles every .sp of the plan; returns the .smx of each source."""
	if not the_plan["sources"]:
		return {}
	spcomp = os.environ.get("SPCOMP")
	if not spcomp:
		raise ActionError("the tests or plugins include .sp files, but spcomp64 is not installed; "
			"set spcomp: true on the setup step")
	compiled = {}
	for source in the_plan["sources"]:
		output = compiled_path(the_plan["results"], source)
		os.makedirs(os.path.dirname(output), exist_ok=True)
		command = [spcomp, source, "-o", output]
		for path in include_paths(the_plan, source):
			command += ["-i", path]
		finished = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
		if finished.returncode != 0:
			print(finished.stdout, end="", flush=True)
			raise ActionError(f"spcomp64 could not compile {source} (exit code {finished.returncode})")
		print(f"::group::Compiled {source}\n{finished.stdout.rstrip()}\n::endgroup::", flush=True)
		compiled[source] = output
	return compiled


def run_runner(the_plan, runner, compiled):
	"""Runs smrunner test with one runner; returns its exit code, JUnit file and report file."""
	results = the_plan["results"]
	junit = os.path.join(results, f"junit-{runner}.xml")
	report = os.path.join(results, f"report-{runner}.txt")
	command = [os.path.join(os.environ["SMRUNNER_HOME"], "bin", runner), "test"]
	command += [compiled.get(test, test) for test in the_plan["tests"]]
	for plugin in the_plan["plugins"]:
		command += ["--plugin", compiled.get(plugin, plugin)]
	command += the_plan["options"] + ["--junit", junit]
	print(f"::group::{runner}", flush=True)
	with open(report, "w", encoding="utf-8") as stream:
		process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
		for line in process.stdout:
			sys.stdout.write(line)
			stream.write(line)
		exit_code = process.wait()
	print("::endgroup::", flush=True)
	return exit_code, junit, report


def read_junit(path, runner):
	"""The test cases of a JUnit file as dicts: runner, plugin, test, status, messages."""
	cases = []
	for case in ElementTree.parse(path).getroot().iter("testcase"):
		status, messages = "PASS", []
		for tag in ("failure", "error", "skipped"):
			element = case.find(tag)
			if element is None:
				continue
			status = {"failure": "FAIL", "skipped": "SKIP"}.get(tag) or element.get("message", "ERROR")
			text = element.text if tag != "skipped" else element.get("message", "")
			messages = [line for line in (text or "").splitlines() if line.strip()]
		cases.append({"runner": runner, "plugin": case.get("classname", ""), "test": case.get("name", ""),
			"status": status, "messages": messages})
	return cases


def repository_files(working_directory):
	"""Every file of the repository, as paths relative to the workspace, by base name."""
	workspace = os.environ.get("GITHUB_WORKSPACE", working_directory)
	listed = subprocess.run(["git", "ls-files", "-z"], cwd=working_directory,
		stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
	if listed.returncode == 0:
		paths = [os.path.join(working_directory, path) for path in listed.stdout.split("\0") if path]
	else:
		paths = [os.path.join(folder, name) for folder, folders, names in os.walk(working_directory)
			if ".git" not in folder.split(os.sep) for name in names]
	by_name = {}
	for path in paths:
		by_name.setdefault(os.path.basename(path), []).append(os.path.relpath(path, workspace))
	return by_name


def annotate_failures(cases, working_directory):
	files = None
	for case in cases:
		if case["status"] in ("PASS", "SKIP"):
			continue
		title = f"{case['runner']}: {case['test']} ({case['plugin']})"
		for message in case["messages"] or [case["status"]]:
			location = LOCATION.search(message)
			if not location:
				annotate("error", message, title)
				continue
			if files is None:
				files = repository_files(working_directory)
			matches = files.get(location.group(1), [])
			if len(matches) == 1:
				annotate("error", message, title, matches[0], location.group(2))
			else:
				annotate("error", message, title)


def cell(text):
	return html.escape(text).replace("|", "&#124;").replace("\n", "<br>")


def summary(runs, cases):
	lines = ["## smrunner\n", "| Runner | Tests | Passed | Failed | Skipped |", "|---|---|---|---|---|"]
	for run in runs:
		mine = [case for case in cases if case["runner"] == run["runner"]]
		failed = sum(case["status"] not in ("PASS", "SKIP") for case in mine)
		passed = sum(case["status"] == "PASS" for case in mine)
		skipped = sum(case["status"] == "SKIP" for case in mine)
		lines.append(f"| {run['runner']} | {len(mine)} | {passed} | {failed} | {skipped} |")
	lines.append("")
	for run in runs:
		if run["problem"]:
			lines.append(f"**{run['runner']}:** {cell(run['problem'])}\n")
	unpassed = [case for case in cases if case["status"] != "PASS"]
	if not unpassed and not any(run["problem"] for run in runs):
		lines.append("All tests passed.\n")
	if unpassed:
		lines += ["| Runner | Test plugin | Test | Status | Details |", "|---|---|---|---|---|"]
		for case in unpassed:
			first = case["messages"][0] if case["messages"] else ""
			rest = "\n".join(case["messages"][1:])
			details = cell(first) if not rest else \
				f"<details><summary>{cell(first)}</summary>{cell(rest)}</details>"
			lines.append(f"| {case['runner']} | {cell(case['plugin'])} | {cell(case['test'])} "
				f"| {case['status']} | {details} |")
		lines.append("")
	return "\n".join(lines) + "\n"


def run(plan_file):
	with open(plan_file, encoding="utf-8") as stream:
		the_plan = json.load(stream)
	if not os.environ.get("SMRUNNER_HOME"):
		raise ActionError("SMRUNNER_HOME is not set; the setup step did not run")
	os.chdir(the_plan["working_directory"])
	compiled = compile_sources(the_plan)

	runs, cases = [], []
	for runner in the_plan["runners"]:
		exit_code, junit, report = run_runner(the_plan, runner, compiled)
		problem = ""
		if os.path.isfile(junit):
			cases += read_junit(junit, runner)
		else:
			problem = f"{runner} exited with code {exit_code} without a test report"
		if exit_code not in (0, 1):
			problem = problem or f"{runner} exited with code {exit_code}"
		if problem:
			annotate("error", problem, "smrunner test")
		runs.append({"runner": runner, "exit_code": exit_code, "junit": junit, "problem": problem})

	if the_plan["annotations"]:
		annotate_failures(cases, the_plan["working_directory"])
	append_summary(summary(runs, cases))
	failed = sum(case["status"] not in ("PASS", "SKIP") for case in cases)
	write_outputs({
		"junit": "\n".join(run["junit"] for run in runs if os.path.isfile(run["junit"])),
		"tests": str(len(cases)),
		"passed": str(sum(case["status"] == "PASS" for case in cases)),
		"failed": str(failed),
		"skipped": str(sum(case["status"] == "SKIP" for case in cases)),
		"result": "success" if all(run["exit_code"] == 0 for run in runs) else "failure",
	})


def main(arguments):
	if arguments[:1] == ["plan"] and len(arguments) == 1:
		plan()
	elif arguments[:1] == ["run"] and len(arguments) == 2:
		run(arguments[1])
	else:
		raise ActionError("usage: run.py plan | run.py run PLAN")


if __name__ == "__main__":
	try:
		main(sys.argv[1:])
	except ActionError as error:
		annotate("error", str(error), "smrunner test")
		sys.exit(1)
