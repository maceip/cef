#!/bin/bash
# Scale self-hosted runner fleet up/down.
#
# Intended to be called by GitHub Actions workflow_run events or manually
# to pre-warm runners before heavy CI runs.
#
# Usage:
#   ./scale-runners.sh up              # Spin up 1 x86 + 1 ARM runner
#   ./scale-runners.sh up 4 2          # Spin up 4 x86 + 2 ARM runners
#   ./scale-runners.sh down            # Scale to zero (cost savings)
#   ./scale-runners.sh status          # Show current fleet status

set -euo pipefail

REGION="${AWS_REGION:-us-east-1}"
X86_ASG="${CEF_X86_ASG:-$(aws cloudformation describe-stacks --region "$REGION" --stack-name CefRunners --query "Stacks[0].Outputs[?OutputKey=='X86AsgName'].OutputValue" --output text 2>/dev/null || echo "")}"
ARM_ASG="${CEF_ARM_ASG:-$(aws cloudformation describe-stacks --region "$REGION" --stack-name CefRunners --query "Stacks[0].Outputs[?OutputKey=='ArmAsgName'].OutputValue" --output text 2>/dev/null || echo "")}"

log() { echo "==> $*"; }

cmd_up() {
  local x86_count="${1:-1}" arm_count="${2:-1}"
  log "Scaling x86 runners to $x86_count, ARM runners to $arm_count"

  [ -n "$X86_ASG" ] && aws autoscaling set-desired-capacity \
    --region "$REGION" \
    --auto-scaling-group-name "$X86_ASG" \
    --desired-capacity "$x86_count"

  [ -n "$ARM_ASG" ] && aws autoscaling set-desired-capacity \
    --region "$REGION" \
    --auto-scaling-group-name "$ARM_ASG" \
    --desired-capacity "$arm_count"

  log "Scaling initiated. Runners will be ready in ~2-3 minutes."
}

cmd_down() {
  log "Scaling all runners to zero"
  [ -n "$X86_ASG" ] && aws autoscaling set-desired-capacity \
    --region "$REGION" --auto-scaling-group-name "$X86_ASG" --desired-capacity 0
  [ -n "$ARM_ASG" ] && aws autoscaling set-desired-capacity \
    --region "$REGION" --auto-scaling-group-name "$ARM_ASG" --desired-capacity 0
  log "Scale-down initiated."
}

cmd_status() {
  echo "--- x86 Fleet ---"
  [ -n "$X86_ASG" ] && aws autoscaling describe-auto-scaling-groups \
    --region "$REGION" --auto-scaling-group-names "$X86_ASG" \
    --query "AutoScalingGroups[0].{Desired:DesiredCapacity,Min:MinSize,Max:MaxSize,InService:Instances[?LifecycleState=='InService'] | length(@)}" \
    --output table || echo "(not configured)"

  echo "--- ARM Fleet ---"
  [ -n "$ARM_ASG" ] && aws autoscaling describe-auto-scaling-groups \
    --region "$REGION" --auto-scaling-group-names "$ARM_ASG" \
    --query "AutoScalingGroups[0].{Desired:DesiredCapacity,Min:MinSize,Max:MaxSize,InService:Instances[?LifecycleState=='InService'] | length(@)}" \
    --output table || echo "(not configured)"
}

case "${1:-status}" in
  up)     cmd_up "${2:-1}" "${3:-1}" ;;
  down)   cmd_down ;;
  status) cmd_status ;;
  *)      echo "Usage: $0 {up [x86-count] [arm-count] | down | status}" ;;
esac
