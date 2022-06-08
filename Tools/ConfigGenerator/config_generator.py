#!/usr/bin/env python3

# If not stated otherwise in this file or this component's license file the
# following copyright and licenses apply:
#
# Copyright 2020 Metrological
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import inspect
import sys
import os
from importlib import machinery, util
import types
import re

from json_helper import JSON
from i_config import *
import traceback

INDENT_SIZE = 2
PARAM_CONFIG = "params.config"
boiler_plate = "from i_config import *"

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir))

import ProxyStubGenerator.Log as Log

NAME = "ConfigGenerator"
VERBOSE = False
SHOW_WARNINGS = True
DOC_ISSUES = False

log = Log.Log(NAME, VERBOSE, SHOW_WARNINGS, DOC_ISSUES)


def file_name(path):
    file_ext = os.path.basename(path)
    index_of_dot = file_ext.index('.')
    name = file_ext[0:index_of_dot]
    return name


def load_module(name, path):
    out = False
    mod = None
    loader = machinery.SourceFileLoader(name, path)
    spec = util.spec_from_loader(loader.name, loader)
    if spec is not None:
        mod = util.module_from_spec(spec)
        try:
            loader.exec_module(mod)
            out = True
        except Exception as exception:
            log.Error(exception.__class__.__name__ + ": ")
            traceback.print_exc()

    return out, mod


def prepend_file(file, line):
    try:
        f = open(file, 'r+')
    except IOError:
        log.Error(f"Error opening File {file}")
    else:
        with f:
            content = f.read()
            f.seek(0, 0)
            f.write(line.rstrip('\r\n') + '\n' + content)


def get_config_params(file):
    try:
        f = open(file)
    except IOError:
        log.Error(f"Error opening File {file}")
        lines = []
    else:
        with f:
            lines = [line.rstrip() for line in f]

    return lines

# Routine to find if a given variable is copied to another variable
# Basically we are finding out if a var is found within brackets as the only way to add
# any variable is by add() method.
def check_assignment(file, var):
    out = False
    try:
        f = open(file)
    except IOError:
        log.Error(f"Error opening File {file}")
    else:
        with f:
            for line in f:
                slist = re.findall(r'\(.*?\)', line)
                for s in slist:
                    if var in s:
                        out = True
                        break;
                if out:
                    break
    return out


if __name__ == "__main__":
    argparser = argparse.ArgumentParser(
        description='JSON Config Builder',
        formatter_class=argparse.RawTextHelpFormatter)

    argparser.add_argument("-l",
                           "--locator",
                           metavar="LibName",
                           action="store",
                           type=str,
                           dest="locator",
                           help="locator of plugin e.g, libWPEFrameworkDeviceInfo.so")

    argparser.add_argument("-c",
                           "--classname",
                           dest="classname",
                           metavar="ClassName",
                           action="store",
                           type=str,
                           help="classname e.g, DeviceInfo")

    argparser.add_argument("-i",
                           "--inputconfig",
                           dest="configfile",
                           metavar="ConfigFile",
                           action="store",
                           type=str,
                           help="CONFIG to read")

    argparser.add_argument("-o",
                           "--outputfile",
                           dest="ofile",
                           metavar="OutputFile",
                           action="store",
                           type=str,
                           help="Output File")

    argparser.add_argument("--indent",
                           dest="indent_size",
                           metavar="SIZE",
                           type=int,
                           action="store",
                           default=INDENT_SIZE,
                           help="code indentation in spaces (default: %i)" % INDENT_SIZE)

    argparser.add_argument("-p",
                           "--paramconfig",
                           dest="params_config",
                           metavar="ParamConfig",
                           action="store",
                           type=str,
                           default=PARAM_CONFIG,
                           help="Full path of File containing Whitelisted params in config file")

    argparser.add_argument("project",
                           metavar="Name",
                           action="store",
                           type=str,
                           help="Project name e.g, DeviceInfo")

    argparser.add_argument("projectdir",
                           metavar="ConfigDir",
                           action="store",
                           type=str,
                           help="Project Working dir")

    args = argparser.parse_args(sys.argv[1:])

    #  log.Print("Preparing Config JSON")
    result = JSON()
    if args.locator:
        result.add("locator", args.locator)
    else:
        result.add("locator", "libWPEFramework" + args.project + ".so")

    if args.classname:
        result.add("classname", args.classname)
    else:
        result.add("classname", args.project)

    if not os.path.exists(args.projectdir):
        log.Error(f"Error: Config Dir path {args.projectdir} doesnt exit\n")
        sys.exit(1)

    if args.configfile:
        cf = args.configfile
    else:
        cf = args.project + ".config"

    args.projectdir = os.path.abspath(os.path.normpath(args.projectdir))
    cf = args.projectdir + "/" + cf

    if os.path.exists(cf):
        prepend_file(cf, boiler_plate)
        res, iconfig = load_module(file_name(cf), cf)
        if not res:
            sys.exit(1)

        if iconfig:
            for name, obj, in inspect.getmembers(iconfig):
                # only classes that are not part of the i_config module
                if inspect.isclass(obj) and not obj.__module__.startswith('i_config'):
                    if issubclass(obj, IConfig):
                        data =  obj.configuration();
                        if isinstance(data, dict | JSON):
                           result.add("configuration", data)
                        else:
                            log.Error(f"Wrong type returned by {name}.configuration() -> {type(data)}")
                    
                    if issubclass(obj, IRoot):
                        data = obj.root()
                        if isinstance(data, dict | JSON):
                            result.add("root", data)
                        else:
                            log.Error(f"Wrong type returned by {name}.root() -> {type(data)}")

                    if issubclass(obj, IPreconditions):
                        data = obj.preconditions()
                        if isinstance(data, list | range):
                            result.add("preconditions", data)
                        else:
                            log.Error(f"Wrong type returned by {name}.preconditions() -> {type(data)}")

                    if issubclass(obj, IAutostart):
                        data = obj.autostart()
                        if isinstance(data, bool):
                            result.add("autostart", data)
                        else:
                            log.Error(f"Wrong type returned by {name}.autostart() -> {type(data)}")
        else:
            log.Error(f"Config File {cf} exists but couldn't load")
            sys.exit(1)
    else:
        log.Print("No Config File")

    if args.ofile:
        of = args.ofile
    else:
        of = args.projectdir + file_name(cf) + ".json"

    log.Print("Writing Config JSON")

    try:
        outfile = open(of, "w")
    except IOError:
        log.Error(f"Error opening Output File {of}")
        sys.exit(1)
    else:
        with outfile:
            outfile.write(result.serialize(args.indent_size))