#!/bin/sh
# Print the current upstream version/fingerprint for one recipe dir.
# pacman -> repo version; aur -> AUR RPC version; url -> github tag or HTTP
# ETag; git -> commit SHA of the ref. Output must be stable for identical
# upstream content. Usage: check-update.sh <dir>
set -eu

RECIPE_DIR="${1:?usage: check-update.sh packages/<app>}"
BUILDER_DIR="$(dirname "$(readlink -f "$0")")"
eval "$(python3 "$BUILDER_DIR/parse-recipe.py" "$RECIPE_DIR")"

_http_fingerprint() { # ETag preferred, Last-Modified fallback
	url="$1"
	etag="$(curl -fsSIL --max-time 60 "$url" | awk -F': ' 'tolower($1)=="etag" {v=$2} END {print v}' | tr -d '\r"' | tr -d ' ')"
	if [ -n "$etag" ]; then
		printf '%s\n' "etag:$etag"
	else
		curl -fsSIL --max-time 60 "$url" | awk -F': ' 'tolower($1)=="last-modified" {v=$2} END {print v}' | tr -d '\r' | tr ' ' '_' | sed 's/^/lastmod:/'
	fi
}

case "$SOURCE_TYPE" in
pacman)
	pacman -Si "$SOURCE_PKG" | awk -F': +' '/^Version/ {print $2; exit}'
	;;
aur)
	curl -fsSL --max-time 60 "https://aur.archlinux.org/rpc/v5/info?arg[]=$SOURCE_PKG" |
		python3 -c "import json,sys; print(json.load(sys.stdin)['results'][0]['Version'])"
	;;
url)
	case "$SOURCE_URL_VERSION" in
	github:*)
		repo="${SOURCE_URL_VERSION#github:}"
		tag="$(curl -fsSL --max-time 60 \
			-H "Authorization: Bearer ${GH_TOKEN:-${GITHUB_TOKEN:-}}" \
			"https://api.github.com/repos/$repo/releases/latest" 2>/dev/null |
			python3 -c "import json,sys; print(json.load(sys.stdin).get('tag_name',''))" 2>/dev/null || true)"
		if [ -n "$tag" ]; then
			printf '%s\n' "$tag"
		else
			_http_fingerprint "$SOURCE_URL" # API rate-limited: degrade to ETag
		fi
		;;
	*)
		_http_fingerprint "$SOURCE_URL"
		;;
	esac
	;;
git)
	git ls-remote "$SOURCE_REPO" "$SOURCE_REF" | awk '{print $1; exit}'
	;;
esac
