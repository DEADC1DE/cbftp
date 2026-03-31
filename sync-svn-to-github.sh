#!/bin/bash

# SVN to GitHub Sync Script
# Syncs cbftp SVN repository to GitHub daily

REPO_DIR="/mnt/cbftp"
LOG_FILE="/home/cbftp/sync.log"
DATE=$(date '+%Y-%m-%d %H:%M:%S')

echo "[$DATE] Starting SVN to GitHub sync..." >> "$LOG_FILE"

# Change to repository directory
cd "$REPO_DIR" || {
    echo "[$DATE] ERROR: Cannot change to $REPO_DIR" >> "$LOG_FILE"
    exit 1
}

# Check if git-svn is installed
if ! git svn --version >/dev/null 2>&1; then
    echo "[$DATE] ERROR: git-svn is not installed. Please install it (e.g., apt-get install git-svn)" >> "$LOG_FILE"
    exit 1
fi

# Switch to master branch
echo "[$DATE] Switching to master branch..." >> "$LOG_FILE"
git checkout master >> "$LOG_FILE" 2>&1 || {
    echo "[$DATE] ERROR: Cannot checkout master branch" >> "$LOG_FILE"
    exit 1
}

# Configure git identity
git config user.name "deadc1de"
git config user.email "deadc1de@dev.org"

# Configure git-svn to accept temporary SSL certificates
git config svn.sslVerify false 2>/dev/null || true

# Fetch latest from GitHub to check for divergence
echo "[$DATE] Fetching from GitHub..." >> "$LOG_FILE"
git fetch origin >> "$LOG_FILE" 2>&1

# Get current HEAD before fetch
BEFORE=$(git rev-parse HEAD)

# Fetch new commits from SVN
echo "[$DATE] Fetching from SVN..." >> "$LOG_FILE"
FETCH_OUTPUT=$(git svn fetch 2>&1)
FETCH_STATUS=$?
echo "$FETCH_OUTPUT" >> "$LOG_FILE"

if [ $FETCH_STATUS -ne 0 ]; then
    echo "[$DATE] ERROR: git svn fetch failed (exit code: $FETCH_STATUS)" >> "$LOG_FILE"

    # Check if it's an SSL certificate issue
    if echo "$FETCH_OUTPUT" | grep -iq "certificate"; then
        echo "[$DATE] SSL certificate issue detected. You may need to renew the SVN server certificate." >> "$LOG_FILE"
    fi
    exit 1
fi

# Get current HEAD after fetch (should be at git-svn)
AFTER=$(git rev-parse refs/remotes/git-svn)

# Ensure master is at git-svn position (no local commits)
echo "[$DATE] Resetting master to git-svn position (removing local commits)" >> "$LOG_FILE"
git reset --hard refs/remotes/git-svn >> "$LOG_FILE" 2>&1

# Check if there are new commits
if [ "$BEFORE" = "$AFTER" ]; then
    echo "[$DATE] No new commits from SVN" >> "$LOG_FILE"
    exit 0
fi

# Count new commits
NEW_COMMITS=$(git rev-list --count $BEFORE..$AFTER)
echo "[$DATE] Found $NEW_COMMITS new commit(s) from SVN" >> "$LOG_FILE"

# Get SVN revision info
SVN_REV=$(git log -1 --format=%B refs/remotes/git-svn | grep "git-svn-id" | sed 's/.*@\([0-9]*\).*/\1/')
SVN_AUTHOR=$(git log -1 --format=%an refs/remotes/git-svn)
SVN_DATE=$(git log -1 --format="%ai" refs/remotes/git-svn)

echo "[$DATE] Last Changed Rev: $SVN_REV" >> "$LOG_FILE"
echo "[$DATE] Last Changed Author: $SVN_AUTHOR" >> "$LOG_FILE"
echo "[$DATE] Last Changed Date: $SVN_DATE" >> "$LOG_FILE"

# Show new commits
echo "[$DATE] New commits:" >> "$LOG_FILE"
git log --oneline $BEFORE..$AFTER >> "$LOG_FILE"

# Push to GitHub (force push since SVN is source of truth)
echo "[$DATE] Pushing to GitHub..." >> "$LOG_FILE"

# Check if we need force push (branch has diverged)
LOCAL_HASH=$(git rev-parse HEAD)
REMOTE_HASH=$(git rev-parse origin/master 2>/dev/null || echo "")

if [ -n "$REMOTE_HASH" ] && ! git merge-base --is-ancestor "$REMOTE_HASH" "$LOCAL_HASH" 2>/dev/null; then
    echo "[$DATE] Branch has diverged from GitHub. Force pushing (SVN is authoritative source)..." >> "$LOG_FILE"
    git push --force-with-lease origin master >> "$LOG_FILE" 2>&1
    PUSH_STATUS=$?
else
    git push origin master >> "$LOG_FILE" 2>&1
    PUSH_STATUS=$?
fi

if [ $PUSH_STATUS -eq 0 ]; then
    echo "[$DATE] Successfully pushed to GitHub" >> "$LOG_FILE"
else
    echo "[$DATE] ERROR: Failed to push to GitHub (exit code: $PUSH_STATUS)" >> "$LOG_FILE"
    echo "[$DATE] Local HEAD: $LOCAL_HASH" >> "$LOG_FILE"
    echo "[$DATE] Remote HEAD: $REMOTE_HASH" >> "$LOG_FILE"
    exit 1
fi

echo "[$DATE] Sync completed successfully" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"

exit 0