#!/usr/bin/env python3
"""AWS CDK stack for CEF build infrastructure.

Provisions:
  - S3 bucket for sccache build cache
  - ECR repositories for build environment images
  - EC2 Auto Scaling Groups for self-hosted GitHub Actions runners (x86 + ARM)
  - EBS snapshot lifecycle for Chromium checkout volumes
  - EventBridge rule + Lambda for CEF release auto-detection
  - IAM roles with least-privilege policies
  - VPC with public subnets (runners need GitHub API access)

Deploy:
  cd infra/cdk && pip install -r requirements.txt
  cdk bootstrap
  cdk deploy --all
"""

import os
from aws_cdk import (
    App,
    Stack,
    Duration,
    RemovalPolicy,
    CfnOutput,
    CfnTag,
    Tags,
    aws_ec2 as ec2,
    aws_ecs as ecs,
    aws_ecr as ecr,
    aws_s3 as s3,
    aws_iam as iam,
    aws_autoscaling as autoscaling,
    aws_lambda as lambda_,
    aws_events as events,
    aws_events_targets as targets,
    aws_dlm as dlm,
    aws_ssm as ssm,
)
from constructs import Construct


class CefBuildVpcStack(Stack):
    """VPC shared by all build infrastructure."""

    def __init__(self, scope: Construct, id: str, **kwargs):
        super().__init__(scope, id, **kwargs)

        self.vpc = ec2.Vpc(
            self, "BuildVpc",
            max_azs=2,
            nat_gateways=1,
            subnet_configuration=[
                ec2.SubnetConfiguration(
                    name="Public", subnet_type=ec2.SubnetType.PUBLIC, cidr_mask=24,
                ),
                ec2.SubnetConfiguration(
                    name="Private", subnet_type=ec2.SubnetType.PRIVATE_WITH_EGRESS, cidr_mask=24,
                ),
            ],
        )

        self.runner_sg = ec2.SecurityGroup(
            self, "RunnerSG",
            vpc=self.vpc,
            description="GitHub Actions self-hosted runners",
            allow_all_outbound=True,
        )

        CfnOutput(self, "VpcId", value=self.vpc.vpc_id)


class CefBuildCacheStack(Stack):
    """S3-backed sccache and ECR image registry."""

    def __init__(self, scope: Construct, id: str, **kwargs):
        super().__init__(scope, id, **kwargs)

        # --- sccache S3 bucket ---
        self.cache_bucket = s3.Bucket(
            self, "SccacheBucket",
            bucket_name=f"cef-build-cache-{self.account}",
            removal_policy=RemovalPolicy.RETAIN,
            auto_delete_objects=False,
            intelligent_tiering_configurations=[
                s3.IntelligentTieringConfiguration(
                    name="AutoTier",
                    archive_access_tier_time=Duration.days(90),
                    deep_archive_access_tier_time=Duration.days(180),
                )
            ],
            lifecycle_rules=[
                s3.LifecycleRule(
                    id="ExpireOldCache",
                    expiration=Duration.days(30),
                    prefix="sccache/",
                ),
            ],
        )

        # --- ECR repos for build images ---
        image_names = ["cef-builder-linux", "cef-builder-linux-arm", "cef-builder-windows"]
        self.repos = {}
        for name in image_names:
            self.repos[name] = ecr.Repository(
                self, f"Ecr{name.replace('-', '')}",
                repository_name=name,
                removal_policy=RemovalPolicy.RETAIN,
                lifecycle_rules=[
                    ecr.LifecycleRule(max_image_count=10, description="Keep last 10 images"),
                ],
            )

        CfnOutput(self, "CacheBucket", value=self.cache_bucket.bucket_name)
        for name, repo in self.repos.items():
            CfnOutput(self, f"Ecr{name}", value=repo.repository_uri)


class CefRunnerStack(Stack):
    """Self-hosted GitHub Actions runners on EC2 with auto-scaling."""

    def __init__(
        self, scope: Construct, id: str, *,
        vpc: ec2.Vpc,
        runner_sg: ec2.SecurityGroup,
        cache_bucket: s3.Bucket,
        **kwargs,
    ):
        super().__init__(scope, id, **kwargs)

        github_token_param = ssm.StringParameter(
            self, "GhRunnerToken",
            parameter_name="/cef/github-runner-token",
            string_value="PLACEHOLDER",
            description="GitHub PAT for runner registration (replace via console/CLI)",
        )

        # --- IAM role for runners ---
        runner_role = iam.Role(
            self, "RunnerRole",
            assumed_by=iam.ServicePrincipal("ec2.amazonaws.com"),
            managed_policies=[
                iam.ManagedPolicy.from_aws_managed_policy_name("AmazonSSMManagedInstanceCore"),
            ],
        )
        cache_bucket.grant_read_write(runner_role)
        github_token_param.grant_read(runner_role)

        # Grant ECR pull
        runner_role.add_to_policy(iam.PolicyStatement(
            actions=["ecr:GetAuthorizationToken", "ecr:BatchGetImage",
                     "ecr:GetDownloadUrlForLayer", "ecr:BatchCheckLayerAvailability"],
            resources=["*"],
        ))

        # --- x86_64 build fleet (c7i.metal-48xl for max parallelism) ---
        x86_user_data = ec2.UserData.for_linux()
        x86_user_data.add_commands(
            "#!/bin/bash",
            "set -euxo pipefail",
            # Install runner dependencies (libicu needed by .NET runtime in GH runner)
            "yum install -y docker git jq libicu",
            "systemctl enable --now docker",
            # Install sccache
            "curl -fsSL https://github.com/mozilla/sccache/releases/download/v0.9.1/sccache-v0.9.1-x86_64-unknown-linux-musl.tar.gz"
            " | tar -xz -C /usr/local/bin --strip-components=1 --wildcards '*/sccache'",
            # Fetch GitHub runner token from SSM
            f"RUNNER_TOKEN=$(aws ssm get-parameter --name /cef/github-runner-token"
            f" --with-decryption --query Parameter.Value --output text --region {self.region})",
            # Create runner user and install GitHub Actions runner
            "useradd -m -s /bin/bash runner",
            "usermod -aG docker runner",
            "mkdir -p /opt/actions-runner && chown runner:runner /opt/actions-runner",
            "cd /opt/actions-runner",
            "curl -fsSL https://github.com/actions/runner/releases/download/v2.322.0/actions-runner-linux-x64-2.322.0.tar.gz"
            " | tar -xz",
            "chown -R runner:runner /opt/actions-runner",
            'RUNNER_NAME="cef-x86-$(hostname)"',
            'su - runner -c "cd /opt/actions-runner && ./config.sh --url https://github.com/maceip/cef'
            " --token $RUNNER_TOKEN"
            ' --name \\"$RUNNER_NAME\\"'
            " --labels self-hosted,linux,x64,cef-builder"
            ' --unattended --replace"',
            "./svc.sh install runner && ./svc.sh start",
        )

        x86_lt = ec2.LaunchTemplate(
            self, "X86LaunchTemplate",
            instance_type=ec2.InstanceType("c7i.8xlarge"),
            machine_image=ec2.MachineImage.latest_amazon_linux2023(),
            role=runner_role,
            security_group=runner_sg,
            user_data=x86_user_data,
            block_devices=[
                ec2.BlockDevice(
                    device_name="/dev/xvda",
                    volume=ec2.BlockDeviceVolume.ebs(200, volume_type=ec2.EbsDeviceVolumeType.GP3),
                ),
                # Chromium checkout volume (restored from snapshot if available)
                ec2.BlockDevice(
                    device_name="/dev/xvdb",
                    volume=ec2.BlockDeviceVolume.ebs(
                        300, volume_type=ec2.EbsDeviceVolumeType.IO2,
                        iops=16000,
                    ),
                ),
            ],
        )

        self.x86_asg = autoscaling.AutoScalingGroup(
            self, "X86Asg",
            vpc=vpc,
            launch_template=x86_lt,
            min_capacity=0,
            max_capacity=4,
            desired_capacity=0,
            vpc_subnets=ec2.SubnetSelection(subnet_type=ec2.SubnetType.PRIVATE_WITH_EGRESS),
            group_metrics=[autoscaling.GroupMetrics.all()],
        )

        # Scale based on CPU: 0 when idle, spin up on demand
        self.x86_asg.scale_on_cpu_utilization(
            "ScaleOnCpu",
            target_utilization_percent=60,
        )

        # --- ARM (Graviton) build fleet for native aarch64 ---
        arm_user_data = ec2.UserData.for_linux()
        arm_user_data.add_commands(
            "#!/bin/bash",
            "set -euxo pipefail",
            "yum install -y docker git jq libicu",
            "systemctl enable --now docker",
            "curl -fsSL https://github.com/mozilla/sccache/releases/download/v0.9.1/sccache-v0.9.1-aarch64-unknown-linux-musl.tar.gz"
            " | tar -xz -C /usr/local/bin --strip-components=1 --wildcards '*/sccache'",
            f"RUNNER_TOKEN=$(aws ssm get-parameter --name /cef/github-runner-token"
            f" --with-decryption --query Parameter.Value --output text --region {self.region})",
            "useradd -m -s /bin/bash runner",
            "usermod -aG docker runner",
            "mkdir -p /opt/actions-runner && chown runner:runner /opt/actions-runner",
            "cd /opt/actions-runner",
            "curl -fsSL https://github.com/actions/runner/releases/download/v2.322.0/actions-runner-linux-arm64-2.322.0.tar.gz"
            " | tar -xz",
            "chown -R runner:runner /opt/actions-runner",
            'RUNNER_NAME="cef-arm-$(hostname)"',
            'su - runner -c "cd /opt/actions-runner && ./config.sh --url https://github.com/maceip/cef'
            " --token $RUNNER_TOKEN"
            ' --name \\"$RUNNER_NAME\\"'
            " --labels self-hosted,linux,arm64,cef-builder-arm"
            ' --unattended --replace"',
            "./svc.sh install runner && ./svc.sh start",
        )

        arm_lt = ec2.LaunchTemplate(
            self, "ArmLaunchTemplate",
            instance_type=ec2.InstanceType("c7g.8xlarge"),
            machine_image=ec2.MachineImage.latest_amazon_linux2023(
                cpu_type=ec2.AmazonLinuxCpuType.ARM_64,
            ),
            role=runner_role,
            security_group=runner_sg,
            user_data=arm_user_data,
            block_devices=[
                ec2.BlockDevice(
                    device_name="/dev/xvda",
                    volume=ec2.BlockDeviceVolume.ebs(200, volume_type=ec2.EbsDeviceVolumeType.GP3),
                ),
            ],
        )

        self.arm_asg = autoscaling.AutoScalingGroup(
            self, "ArmAsg",
            vpc=vpc,
            launch_template=arm_lt,
            min_capacity=0,
            max_capacity=2,
            desired_capacity=0,
            vpc_subnets=ec2.SubnetSelection(subnet_type=ec2.SubnetType.PRIVATE_WITH_EGRESS),
        )

        CfnOutput(self, "X86AsgName", value=self.x86_asg.auto_scaling_group_name)
        CfnOutput(self, "ArmAsgName", value=self.arm_asg.auto_scaling_group_name)


class CefSnapshotStack(Stack):
    """DLM lifecycle policy for Chromium checkout EBS snapshots."""

    def __init__(self, scope: Construct, id: str, **kwargs):
        super().__init__(scope, id, **kwargs)

        dlm_role = iam.Role(
            self, "DlmRole",
            assumed_by=iam.ServicePrincipal("dlm.amazonaws.com"),
            managed_policies=[
                iam.ManagedPolicy.from_aws_managed_policy_name(
                    "service-role/AWSDataLifecycleManagerServiceRole"
                ),
            ],
        )

        dlm.CfnLifecyclePolicy(
            self, "ChromiumSnapshotPolicy",
            description="Weekly snapshot of Chromium checkout volumes",
            state="ENABLED",
            execution_role_arn=dlm_role.role_arn,
            policy_details=dlm.CfnLifecyclePolicy.PolicyDetailsProperty(
                resource_types=["VOLUME"],
                target_tags=[CfnTag(key="cef:chromium-checkout", value="true")],
                schedules=[
                    dlm.CfnLifecyclePolicy.ScheduleProperty(
                        name="WeeklySnapshot",
                        create_rule=dlm.CfnLifecyclePolicy.CreateRuleProperty(
                            cron_expression="cron(0 3 ? * SUN *)",  # weekly on Sunday 03:00 UTC
                        ),
                        retain_rule=dlm.CfnLifecyclePolicy.RetainRuleProperty(count=3),
                        copy_tags=True,
                    ),
                ],
            ),
        )


class CefReleaseDetectorStack(Stack):
    """EventBridge + Lambda to detect new CEF releases and trigger bindings update."""

    def __init__(self, scope: Construct, id: str, **kwargs):
        super().__init__(scope, id, **kwargs)

        detector_fn = lambda_.Function(
            self, "CefReleaseDetector",
            runtime=lambda_.Runtime.PYTHON_3_12,
            handler="index.handler",
            timeout=Duration.seconds(60),
            memory_size=256,
            environment={
                "GITHUB_REPO": "maceip/cef",
                "SSM_TOKEN_PARAM": "/cef/github-runner-token",
            },
            code=lambda_.Code.from_inline(RELEASE_DETECTOR_CODE),
        )

        # Grant permissions to trigger GitHub workflow
        detector_fn.add_to_role_policy(iam.PolicyStatement(
            actions=["ssm:GetParameter"],
            resources=[f"arn:aws:ssm:{self.region}:{self.account}:parameter/cef/github-runner-token"],
        ))

        # Run every 6 hours (more responsive than daily cron in get-latest.yml)
        events.Rule(
            self, "ScheduleRule",
            schedule=events.Schedule.rate(Duration.hours(6)),
            targets=[targets.LambdaFunction(detector_fn)],
        )

        CfnOutput(self, "DetectorFnName", value=detector_fn.function_name)


# Lambda inline code for release detection
RELEASE_DETECTOR_CODE = '''
import json
import os
import urllib.request
import boto3

CEF_RELEASES_URL = "https://cef-builds.spotifycdn.com/index.json"

def handler(event, context):
    """Check for new CEF releases and trigger get-latest workflow if found."""
    ssm = boto3.client("ssm")

    # Check latest CEF version from Spotify CDN
    req = urllib.request.Request(CEF_RELEASES_URL, headers={"User-Agent": "cef-release-detector/1.0"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read())

    # Get current known version from SSM (or "unknown")
    param_name = "/cef/latest-known-version"
    try:
        current = ssm.get_parameter(Name=param_name)["Parameter"]["Value"]
    except ssm.exceptions.ParameterNotFound:
        current = "unknown"

    # Find latest stable linux64 version
    linux64 = data.get("linux64", {}).get("versions", [])
    stable = [v for v in linux64 if v.get("channel") == "stable"]
    if not stable:
        print("No stable releases found")
        return {"statusCode": 200, "body": "no stable releases"}

    latest = stable[0].get("cef_version", "unknown")
    print(f"Current: {current}, Latest: {latest}")

    if latest == current:
        print("Already up to date")
        return {"statusCode": 200, "body": "up to date"}

    # New version detected — trigger GitHub workflow
    token_param = ssm.get_parameter(Name=os.environ["SSM_TOKEN_PARAM"], WithDecryption=True)
    token = token_param["Parameter"]["Value"]

    repo = os.environ["GITHUB_REPO"]
    url = f"https://api.github.com/repos/{repo}/actions/workflows/get-latest.yml/dispatches"
    payload = json.dumps({"ref": "dev"}).encode()
    req = urllib.request.Request(
        url, data=payload, method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github.v3+json",
            "User-Agent": "cef-release-detector/1.0",
        },
    )
    urllib.request.urlopen(req, timeout=15)

    # Update known version
    ssm.put_parameter(Name=param_name, Value=latest, Type="String", Overwrite=True)
    print(f"Triggered get-latest for {latest}")
    return {"statusCode": 200, "body": f"triggered for {latest}"}
'''


# --- App entrypoint ---
app = App()

vpc_stack = CefBuildVpcStack(app, "CefBuildVpc",
    description="VPC for CEF build infrastructure")

cache_stack = CefBuildCacheStack(app, "CefBuildCache",
    description="S3 sccache + ECR image registry")

runner_stack = CefRunnerStack(app, "CefRunners",
    vpc=vpc_stack.vpc,
    runner_sg=vpc_stack.runner_sg,
    cache_bucket=cache_stack.cache_bucket,
    description="Self-hosted GitHub Actions runners (x86 + ARM)")

snapshot_stack = CefSnapshotStack(app, "CefSnapshots",
    description="EBS snapshot lifecycle for Chromium checkout volumes")

release_stack = CefReleaseDetectorStack(app, "CefReleaseDetector",
    description="Lambda to detect new CEF releases and trigger CI")

for stack in [vpc_stack, cache_stack, runner_stack, snapshot_stack, release_stack]:
    Tags.of(stack).add("project", "cef")
    Tags.of(stack).add("managed-by", "cdk")

app.synth()
