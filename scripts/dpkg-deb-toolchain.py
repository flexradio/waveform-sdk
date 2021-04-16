#!/usr/bin/python3

from urllib.request import urlopen
from gzip import GzipFile
from debian import deb822
import subprocess
import argparse
import sys

mirror = "http://ftp.debian.org/debian"
release = 'buster'
arch = 'arm64'

verboten_packages = {
    'libc6',
    'libgcc1',
    'gcc-8-base',
    'libstdc++6',
    'libstdc++-6-dev',
    'libc6-dev'
}


def get_packages():
    package_list = {}

    with urlopen(f'{mirror}/dists/{release}/main/binary-{arch}/Packages.gz') as packages:
        for package in deb822.Packages.iter_paragraphs(GzipFile(fileobj=packages),
                                                       fields=['Package', 'Filename', 'Depends']):
            package_list[package['Package']] = {
                'Filename': package['Filename'],
                'Depends': set([relation[0]['name'] for relation in
                                deb822.PkgRelation.parse_relations(package["Depends"])] if 'Depends' in package else [])
            }

    return package_list


def get_deps_from_control(control_filename):
    with open(control_filename) as control_file:
        control = deb822.Deb822(control_file)
        return set([relation[0]['name'] for relation in deb822.PkgRelation.parse_relations(control["Build-Depends"])])


def get_package_dependencies(name, current_deps, packages):
    depends = set()

    if name in ['python3', 'perl']:
        return depends

    try:
        p = packages[name]
    except KeyError as e:
        print(f"Package {name} not found")
        return depends

    if 'Depends' not in p:
        print(f'Package {name} has no dependencies')
        return depends

    depends = p['Depends'] - verboten_packages
    new_deps = current_deps - depends
    for dependency in new_deps:
        depends |= get_package_dependencies(dependency, current_deps | depends, packages)

    return depends


def install_package(name, install_path, packages):
    url = mirror + '/' + packages[name]['Filename']
    with urlopen(url) as package:
        try:
            subprocess.run(['dpkg-deb', '-x', '-', install_path], input=package.read(), check=True)
        except subprocess.SubprocessError as e:
            print(f'Cannot install package {name}')
            print(package.info())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(prog='dpkg-deb-toolchain')
    parser.add_argument('--control', required=True, help='Control File')
    parser.add_argument('--install', required=True, help='Install Dir')

    args = parser.parse_args()

    packages = get_packages()

    deps = get_deps_from_control(args.control)
    # We need a copy here because we can't change the set while iterating
    for dep in deps.copy():
        deps |= {dep}
        deps |= get_package_dependencies(dep, set(), packages)

    for dep in deps:
        install_package(dep, args.install, packages)
