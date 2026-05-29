#!/usr/bin/env bash
set -euo pipefail

BUMP="${1:-patch}"

case "$BUMP" in
  patch|minor|major) ;;
  *) echo "Usage: $0 [patch|minor|major]" >&2; exit 1 ;;
esac

latest_tag=$(gh release list --limit 1 --json tagName --jq '.[0].tagName')
version="${latest_tag#v}"

IFS='.' read -r major minor patch <<< "$version"

case "$BUMP" in
  patch) patch=$((patch + 1)) ;;
  minor) minor=$((minor + 1)); patch=0 ;;
  major) major=$((major + 1)); minor=0; patch=0 ;;
esac

new_version="${major}.${minor}.${patch}"
new_tag="v${new_version}"

echo "Latest release: ${latest_tag}"
echo "Creating release: ${new_tag}"

gh release create "$new_tag" \
  --title "$new_version" \
  --generate-notes

echo "Release ${new_tag} created. Waiting for Release workflow run..."

repo=$(gh repo view --json nameWithOwner --jq '.nameWithOwner')
wait_limit=30

for i in $(seq 1 $wait_limit); do
  run=$(gh run list \
    --repo "$repo" \
    --workflow "release.yml" \
    --limit 5 \
    --json databaseId,createdAt,headBranch,event,url,status \
    --jq "[.[] | select(.event == \"release\" and .headBranch == \"${new_tag}\")] | sort_by(.createdAt) | last")

  if [[ -n "$run" && "$run" != "null" ]]; then
    run_url=$(echo "$run" | python3 -c "import sys,json; print(json.load(sys.stdin)['url'])")
    created_at=$(echo "$run" | python3 -c "import sys,json; print(json.load(sys.stdin)['createdAt'])")
    echo "Workflow run started (created ${created_at}):"
    echo "  ${run_url}"
    exit 0
  fi

  echo "  Waiting... (${i}/${wait_limit})"
  sleep 0.4
done

echo "Timed out waiting for workflow run. Check manually:" >&2
echo "  https://github.com/${repo}/actions/workflows/release.yml" >&2
exit 1
