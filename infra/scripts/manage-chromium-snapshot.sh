#!/bin/bash
# Manage EBS snapshots of the Chromium checkout volume.
#
# A fully-synced Chromium checkout is ~30GB and takes 30-60 min to gclient sync.
# Snapshotting it to EBS lets new build instances restore in ~2 minutes.
#
# Usage:
#   ./manage-chromium-snapshot.sh create     # Snapshot current volume
#   ./manage-chromium-snapshot.sh restore    # Create volume from latest snapshot
#   ./manage-chromium-snapshot.sh list       # List available snapshots
#   ./manage-chromium-snapshot.sh cleanup    # Delete old snapshots (keep last 3)

set -euo pipefail

TAG_KEY="cef:chromium-checkout"
TAG_VALUE="true"
REGION="${AWS_REGION:-us-east-1}"

log() { echo "==> $*"; }
err() { echo "ERROR: $*" >&2; exit 1; }

cmd_create() {
  local volume_id="${1:-}"
  if [ -z "$volume_id" ]; then
    # Auto-detect: find volume tagged with cef:chromium-checkout on this instance
    local instance_id
    instance_id=$(curl -s http://169.254.169.254/latest/meta-data/instance-id)
    volume_id=$(aws ec2 describe-volumes \
      --region "$REGION" \
      --filters "Name=attachment.instance-id,Values=$instance_id" \
                "Name=tag:$TAG_KEY,Values=$TAG_VALUE" \
      --query "Volumes[0].VolumeId" --output text)
    [ "$volume_id" = "None" ] && err "No tagged Chromium checkout volume found on this instance"
  fi

  log "Creating snapshot of volume $volume_id..."
  local snapshot_id
  snapshot_id=$(aws ec2 create-snapshot \
    --region "$REGION" \
    --volume-id "$volume_id" \
    --description "Chromium checkout $(date +%Y-%m-%d)" \
    --tag-specifications "ResourceType=snapshot,Tags=[{Key=$TAG_KEY,Value=$TAG_VALUE},{Key=Name,Value=chromium-checkout-$(date +%Y%m%d)}]" \
    --query "SnapshotId" --output text)

  log "Snapshot created: $snapshot_id"
  log "Waiting for completion..."
  aws ec2 wait snapshot-completed --region "$REGION" --snapshot-ids "$snapshot_id"
  log "Done."
}

cmd_restore() {
  local az="${1:-us-east-1a}"
  local snapshot_id
  snapshot_id=$(aws ec2 describe-snapshots \
    --region "$REGION" \
    --filters "Name=tag:$TAG_KEY,Values=$TAG_VALUE" \
    --query "Snapshots | sort_by(@, &StartTime) | [-1].SnapshotId" --output text)

  [ "$snapshot_id" = "None" ] && err "No Chromium checkout snapshots found"
  log "Restoring from snapshot $snapshot_id..."

  local volume_id
  volume_id=$(aws ec2 create-volume \
    --region "$REGION" \
    --availability-zone "$az" \
    --snapshot-id "$snapshot_id" \
    --volume-type io2 \
    --iops 16000 \
    --tag-specifications "ResourceType=volume,Tags=[{Key=$TAG_KEY,Value=$TAG_VALUE},{Key=Name,Value=chromium-checkout}]" \
    --query "VolumeId" --output text)

  log "Volume created: $volume_id (from $snapshot_id)"
  log "Waiting for availability..."
  aws ec2 wait volume-available --region "$REGION" --volume-ids "$volume_id"
  log "Ready. Attach with: aws ec2 attach-volume --volume-id $volume_id --instance-id <id> --device /dev/xvdb"
}

cmd_list() {
  aws ec2 describe-snapshots \
    --region "$REGION" \
    --filters "Name=tag:$TAG_KEY,Values=$TAG_VALUE" \
    --query "Snapshots[].{Id:SnapshotId,Date:StartTime,Size:VolumeSize,State:State}" \
    --output table
}

cmd_cleanup() {
  local keep="${1:-3}"
  local snapshots
  snapshots=$(aws ec2 describe-snapshots \
    --region "$REGION" \
    --filters "Name=tag:$TAG_KEY,Values=$TAG_VALUE" \
    --query "Snapshots | sort_by(@, &StartTime) | [:-${keep}].SnapshotId" --output text)

  if [ -z "$snapshots" ] || [ "$snapshots" = "None" ]; then
    log "Nothing to clean up (keeping last $keep snapshots)"
    return
  fi

  for sid in $snapshots; do
    log "Deleting old snapshot: $sid"
    aws ec2 delete-snapshot --region "$REGION" --snapshot-id "$sid"
  done
  log "Cleanup done."
}

case "${1:-help}" in
  create)  cmd_create "${2:-}" ;;
  restore) cmd_restore "${2:-us-east-1a}" ;;
  list)    cmd_list ;;
  cleanup) cmd_cleanup "${2:-3}" ;;
  *)
    echo "Usage: $0 {create [volume-id]|restore [az]|list|cleanup [keep-count]}"
    ;;
esac
