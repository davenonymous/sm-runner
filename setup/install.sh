#!/usr/bin/env bash
# Copyright 2026 davenonymous
#
# Installs smrunner for the setup and test actions, on Linux runners.
#
#   install.sh resolve   writes the version, the cache key and the cache
#                        paths to $GITHUB_OUTPUT
#   install.sh install   installs smrunner (and spcomp64), the 32-bit
#                        runtime, and exports PATH and the SMRUNNER_*,
#                        SPCOMP and SOURCEMOD_INCLUDE variables
#
# Its inputs come from the environment, as the actions' inputs:
#   SETUP_VERSION, SETUP_ARCHIVE, SETUP_SPCOMP, SETUP_SOURCEMOD_URL,
#   SETUP_SOURCEMOD_SHA256, SETUP_32BIT_RUNTIME, SETUP_ACTION_REF,
#   SETUP_ACTION_REPOSITORY
#
# Without SETUP_VERSION the version is the one in packages/runner/src/version.h
# of the action's own checkout, if the action is used at a v* tag. Releases
# come from the repository the action was used from. Everything is installed
# below $RUNNER_TEMP/smrunner-setup; an installation that is already complete
# there (restored from the cache) is used as it is.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fallback_repository="davenonymous/sm-runner"

fail() {
	echo "::error title=smrunner setup::$*"
	exit 1
}

# true or false, as the inputs take them; anything else is refused.
is_true() {
	case "$1" in
		true) return 0 ;;
		false) return 1 ;;
		*) fail "$2 must be true or false, not \"$1\"" ;;
	esac
}

parse_inputs() {
	[ "${RUNNER_OS:-}" = "Linux" ] || fail "smrunner runs on Linux runners only, this one is ${RUNNER_OS:-unknown}"
	[ -n "${RUNNER_TEMP:-}" ] || fail "RUNNER_TEMP is not set; run this inside a GitHub Actions job"

	version_input="${SETUP_VERSION:-}"
	archive="${SETUP_ARCHIVE:-}"
	sourcemod_url="${SETUP_SOURCEMOD_URL:-}"
	sourcemod_sha256="${SETUP_SOURCEMOD_SHA256:-}"
	wants_spcomp=false
	is_true "${SETUP_SPCOMP:-false}" spcomp && wants_spcomp=true
	wants_32bit_runtime=false
	is_true "${SETUP_32BIT_RUNTIME:-true}" install-32bit-runtime && wants_32bit_runtime=true

	if [ -n "${archive}" ] && [ -n "${version_input}" ]; then
		fail "set either version or archive, not both"
	fi
	if [ -n "${archive}" ]; then
		[ -f "${archive}" ] || fail "archive ${archive} is not a file"
		archive="$(realpath "${archive}")"
	fi
	if [ "${wants_spcomp}" = false ] && { [ -n "${sourcemod_url}" ] || [ -n "${sourcemod_sha256}" ]; }; then
		fail "sourcemod-url and sourcemod-sha256 need spcomp: true"
	fi
	if [ -z "${sourcemod_url}" ] && [ -n "${sourcemod_sha256}" ]; then
		fail "sourcemod-sha256 needs sourcemod-url"
	fi
	if [ -z "${sourcemod_url}" ]; then
		# shellcheck source=sourcemod.env
		. "${here}/sourcemod.env"
		sourcemod_url="${SOURCEMOD_PACKAGE_URL}"
		sourcemod_sha256="${SOURCEMOD_PACKAGE_SHA256}"
	fi
}

# The action's repository and ref, from the context or else from where the
# runner checked it out: .../_actions/<owner>/<repo>/<ref>/setup.
locate_action() {
	action_ref="${SETUP_ACTION_REF:-}"
	repository="${SETUP_ACTION_REPOSITORY:-}"
	if [[ "${here}" =~ /_actions/([^/]+)/([^/]+)/(.+)/setup$ ]]; then
		repository="${repository:-${BASH_REMATCH[1]}/${BASH_REMATCH[2]}}"
		action_ref="${action_ref:-${BASH_REMATCH[3]}}"
	fi
	repository="${repository:-${fallback_repository}}"
}

resolve_version() {
	if [ -n "${archive}" ]; then
		version="archive"
		return
	fi
	if [ -n "${version_input}" ]; then
		version="${version_input#v}"
	elif [[ "${action_ref}" =~ ^v[0-9] ]]; then
		version="$(sed -n 's/^#define SMRUNNER_VERSION "\(.*\)"$/\1/p' "${here}/../packages/runner/src/version.h")"
	else
		fail "set version: the action is used at \"${action_ref:-a local path}\", which is not a v* tag"
	fi
	[[ "${version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.-]+)?$ ]] ||
		fail "\"${version}\" is not a smrunner version such as 0.1.0"
}

resolve_paths() {
	root="${RUNNER_TEMP}/smrunner-setup"
	smrunner_dir="${root}/smrunner-${version}"
	home="${smrunner_dir}/smrunner"
	sourcemod_dir="${root}/sourcemod-$(printf '%s' "${sourcemod_url}" | sha256sum | cut -c1-16)"
	scripting="${sourcemod_dir}/addons/sourcemod/scripting"
	cache_key="smrunner-setup-${RUNNER_OS}-${version}"
	cache_paths="${smrunner_dir}"
	if [ "${wants_spcomp}" = true ]; then
		cache_key="${cache_key}-${sourcemod_dir##*/}"
		cache_paths="${cache_paths}"$'\n'"${sourcemod_dir}"
	fi
}

download() {
	curl -fsSL --retry 3 -o "$2" "$1" || fail "could not download $1"
}

verify_sha256() {
	local actual
	actual="$(sha256sum "$1" | cut -d' ' -f1)"
	[ "${actual}" = "$2" ] || fail "$3 has SHA-256 ${actual}, expected $2"
}

install_smrunner() {
	if [ -z "${archive}" ] && [ -f "${home}/.installed" ]; then
		echo "smrunner ${version} is already installed in ${home}"
		return
	fi
	rm -rf "${smrunner_dir}"
	mkdir -p "${smrunner_dir}"
	if [ -n "${archive}" ]; then
		echo "Unpacking ${archive}"
		tar -xzf "${archive}" -C "${smrunner_dir}"
	else
		local name="smrunner-v${version}-linux.tar.gz"
		local base="https://github.com/${repository}/releases/download/v${version}"
		echo "Downloading ${base}/${name}"
		download "${base}/${name}" "${smrunner_dir}/${name}"
		download "${base}/${name}.sha256" "${smrunner_dir}/${name}.sha256"
		verify_sha256 "${smrunner_dir}/${name}" "$(cut -d' ' -f1 < "${smrunner_dir}/${name}.sha256")" "${name}"
		tar -xzf "${smrunner_dir}/${name}" -C "${smrunner_dir}"
		rm "${smrunner_dir}/${name}" "${smrunner_dir}/${name}.sha256"
	fi
	[ -x "${home}/bin/smrunner64" ] || fail "the archive does not hold smrunner/bin/smrunner64"
	touch "${home}/.installed"
}

install_spcomp() {
	if [ -f "${sourcemod_dir}/.installed" ]; then
		echo "spcomp64 is already installed in ${scripting}"
		return
	fi
	rm -rf "${sourcemod_dir}"
	mkdir -p "${sourcemod_dir}"
	echo "Downloading ${sourcemod_url}"
	download "${sourcemod_url}" "${sourcemod_dir}/sourcemod.tar.gz"
	if [ -n "${sourcemod_sha256}" ]; then
		verify_sha256 "${sourcemod_dir}/sourcemod.tar.gz" "${sourcemod_sha256}" "${sourcemod_url}"
	else
		echo "::warning title=smrunner setup::${sourcemod_url} was not verified; set sourcemod-sha256 to check it"
	fi
	tar -xzf "${sourcemod_dir}/sourcemod.tar.gz" -C "${sourcemod_dir}" addons/sourcemod/scripting ||
		fail "${sourcemod_url} has no addons/sourcemod/scripting"
	rm "${sourcemod_dir}/sourcemod.tar.gz"
	[ -x "${scripting}/spcomp64" ] || fail "${sourcemod_url} has no addons/sourcemod/scripting/spcomp64"
	touch "${sourcemod_dir}/.installed"
}

install_32bit_runtime() {
	local sudo=""
	[ "$(id -u)" = 0 ] || sudo="sudo"
	echo "Installing the 32-bit runtime (libc6-i386, lib32stdc++6)"
	${sudo} apt-get update -q
	${sudo} apt-get install -yq --no-install-recommends libc6-i386 lib32stdc++6
}

export_environment() {
	echo "${home}/bin" >> "${GITHUB_PATH}"
	{
		echo "SMRUNNER_HOME=${home}"
		echo "SMRUNNER_INCLUDE=${home}/share/include"
	} >> "${GITHUB_ENV}"
	if [ "${wants_spcomp}" = true ]; then
		echo "${scripting}" >> "${GITHUB_PATH}"
		{
			echo "SPCOMP=${scripting}/spcomp64"
			echo "SOURCEMOD_INCLUDE=${scripting}/include"
		} >> "${GITHUB_ENV}"
	fi
}

mode="${1:-}"
case "${mode}" in
	resolve | install) ;;
	*) fail "usage: install.sh resolve|install" ;;
esac
parse_inputs
locate_action
resolve_version
resolve_paths

if [ "${mode}" = resolve ]; then
	{
		echo "version=${version}"
		echo "cache-key=${cache_key}"
		echo "cache-paths<<SMRUNNER_EOF"
		echo "${cache_paths}"
		echo "SMRUNNER_EOF"
	} >> "${GITHUB_OUTPUT}"
	exit 0
fi

install_smrunner
[ "${wants_spcomp}" = false ] || install_spcomp
[ "${wants_32bit_runtime}" = false ] || install_32bit_runtime
export_environment
"${home}/bin/smrunner64" --version
