# template.pkr.hcl
packer {
  required_plugins {
    amazon = {
      version = ">= 0.0.2"
      source  = "github.com/hashicorp/amazon"
    }
  }
}

// variables must be passed in the command line. Used for the github actions workflows.
variable "aws_access_key" {
  type        = string
  description = "AWS access key"
}

variable "aws_secret_key" {
  type        = string
  description = "AWS secret access key"
}

variable "aws_region" {
  type        = string
  description = "AWS region"
}

variable "version" {
  type        = string
  description = "GridLAB-D Version"
}

variable "tagname" {
  type        = string
  description = "GridLAB-D Full Version Tag Name"
}

source "amazon-ebs" "ubuntu-22-04" {
  force_deregister     = true // If AMI of same name exists and is owned, we can replace it with this option
  source_ami           = "ami-014d05e6b24240371"
  region               = var.aws_region
  instance_type        = "t2.micro"
  ssh_username         = "ubuntu"
  ami_name             = var.version

  launch_block_device_mappings {
    device_name           = "/dev/sda1"
    volume_size           = 25
    volume_type           = "gp2"
    delete_on_termination = true
  }

  tags = {
    Name        = var.tagname
    CreatedBy   = "Packer"
    Environment = "Dev"
  }
}

build {
  sources = ["source.amazon-ebs.ubuntu-22-04"]

  provisioner "shell" {
    inline = [
      "sudo apt-get remove unattended-upgrades -y",
      "sudo mkdir -p /usr/local/var",
      "sudo mkdir -p /usr/local/src",
      "sudo chown -R $USER /usr/local",
      "sudo apt-get update && sudo apt-get install -y apt-transport-https",
      "sudo apt-get install -y git curl nano",
      "cd /usr/local/src",
      "export INSTALL_SOURCE=https://install-dev.arras.energy",
      "curl --retry 5 -sL https://install-dev.arras.energy/install.sh | sudo -E sh",
      "sudo chown -R $USER /usr/local",
      "gridlabd --version=all"
    ]
  }
}
