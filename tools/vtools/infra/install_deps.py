#!/usr/bin/env python3

import argparse
import logging
import os
import shutil
import sys
from urllib import request

root_dir = os.path.join(os.path.dirname(__file__), '..')

sys.path.append(os.path.join(root_dir, 'tools'))

import log
import shell

logger = logging.getLogger('rp')
install_dir = os.path.abspath(os.path.join(root_dir, 'build', 'infra'))


def get_terraform_path():
    return os.path.join(install_dir, 'terraform')


def check_deps_installed():
    return _check_installed('terraform') and _check_installed('aws2')


def install_deps():
    os.makedirs(install_dir, exist_ok=True)
    _install_awscli()
    _install_terraform()


def _install_awscli():
    awscli_zip = os.path.join(install_dir, 'awscliv2.zip')
    awscli_url = 'https://d1vvhvl2y92vvt.cloudfront.net/awscli-exe-linux-x86_64.zip'
    install_cmd = f"""{os.path.join(install_dir, 'aws', 'install')} \
    --install-dir {install_dir} \
    --bin-dir {install_dir}"""
    logger.info('Downloading AWS CLI v2...')
    _download_and_extract(awscli_url, awscli_zip, install_dir)
    if _check_installed('aws2'):
        logger.info('Found existing AWS CLI v2 installation. Updating...')
        shell.run_subprocess(f'{install_cmd} --update')
    else:
        shell.run_subprocess(install_cmd)
        shell.run_subprocess(f'{os.path.join(install_dir, "aws2")} configure')
    shutil.rmtree(os.path.join(install_dir, 'aws'))


def _install_terraform():
    tf_zip = os.path.join(install_dir, 'terraform.zip')
    tf_url = 'https://releases.hashicorp.com/terraform/0.12.15/terraform_0.12.15_linux_amd64.zip'
    logger.info('Downloading Terraform...')
    _download_and_extract(tf_url, tf_zip, install_dir)


def _check_installed(name):
    return os.path.isfile(os.path.join(install_dir, name))


def _download_and_extract(url, dest, extract_to):
    with request.urlopen(url) as res, open(dest, 'wb') as out:
        shutil.copyfileobj(res, out)
    # The exctraction has to be done with unzip because ZipFile has a bug and
    # it does not preserve file permissions.
    # See https://bugs.python.org/issue15795
    shell.run_subprocess(f'unzip -o -d {extract_to} {dest}')
    os.remove(dest)


def main():
    parser = _build_parser()
    opts = parser.parse_args()
    log.set_logger_for_main(getattr(logging, opts.log.upper()))
    install_deps()


def _build_parser():
    parser = argparse.ArgumentParser(description='Deploy test environments')
    parser.add_argument(
        '--log',
        type=str,
        choices=['critical', 'error', 'warning', 'info', 'debug'],
        default='info',
        help="The log level")
    return parser


if __name__ == '__main__':
    main()
