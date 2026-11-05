#!/bin/bash
# Applies the public-repo settings for Tonetrace on GitHub. Safe to run again: existing rulesets are updated in place.
#   tools/github/apply-repo-settings.sh [owner/repo]     (default: taiigris/tonetrace)
# Needs the GitHub CLI, logged in as a repo admin: brew install gh && gh auth login
set -euo pipefail
cd "$(dirname "$0")"

REPO="${1:-taiigris/tonetrace}"
command -v gh >/dev/null || { echo "gh is missing: brew install gh && gh auth login"; exit 1; }
gh auth status >/dev/null 2>&1 || { echo "gh is not logged in: gh auth login"; exit 1; }

step() { printf '==> %s\n' "$1"; }
api() { gh api -H "Accept: application/vnd.github+json" -H "X-GitHub-Api-Version: 2022-11-28" "$@"; }

step "Merge options: squash only, PR title + body, delete branches, auto-merge, update branch"
api -X PATCH "repos/$REPO" --silent \
  -F allow_squash_merge=true -F allow_merge_commit=false -F allow_rebase_merge=false \
  -f squash_merge_commit_title=PR_TITLE -f squash_merge_commit_message=PR_BODY \
  -F delete_branch_on_merge=true -F allow_auto_merge=true -F allow_update_branch=true \
  -F has_issues=true -F has_wiki=false -F has_projects=false

step "Security: secret scanning + push protection, Dependabot alerts + fixes, private vulnerability reporting"
api -X PATCH "repos/$REPO" --silent --input - <<'JSON'
{ "security_and_analysis": {
    "secret_scanning": { "status": "enabled" },
    "secret_scanning_push_protection": { "status": "enabled" } } }
JSON
api -X PUT "repos/$REPO/vulnerability-alerts" --silent
api -X PUT "repos/$REPO/automated-security-fixes" --silent
api -X PUT "repos/$REPO/private-vulnerability-reporting" --silent

step "Actions: read-only token by default, workflows may not approve PRs, first-time contributors need approval"
api -X PUT "repos/$REPO/actions/permissions/workflow" --silent \
  -f default_workflow_permissions=read -F can_approve_pull_request_reviews=false
api -X PUT "repos/$REPO/actions/permissions/fork-pr-contributor-approval" --silent \
  -f approval_policy=first_time_contributors

upsert_ruleset() {
  local file="$1" name id
  name="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["name"])' "$file")"
  id="$(api "repos/$REPO/rulesets" --jq ".[] | select(.name == \"$name\") | .id" | head -n 1)"
  if [[ -n "$id" ]]; then
    step "Ruleset \"$name\": update ($id)"
    api -X PUT "repos/$REPO/rulesets/$id" --input "$file" --silent
  else
    step "Ruleset \"$name\": create"
    api -X POST "repos/$REPO/rulesets" --input "$file" --silent
  fi
}
upsert_ruleset ruleset-main.json
upsert_ruleset ruleset-tags.json

step "Done. Current merge settings:"
api "repos/$REPO" --jq '{squash: .allow_squash_merge, merge_commit: .allow_merge_commit, rebase: .allow_rebase_merge,
  delete_branch_on_merge, allow_auto_merge, secret_scanning: .security_and_analysis.secret_scanning.status,
  push_protection: .security_and_analysis.secret_scanning_push_protection.status}'
api "repos/$REPO/rulesets" --jq '.[] | "ruleset: \(.name) (\(.enforcement))"'
