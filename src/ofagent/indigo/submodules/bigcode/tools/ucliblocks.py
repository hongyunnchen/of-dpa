#!/usr/bin/env python
################################################################
#
#        Copyright 2013, Big Switch Networks, Inc.
#
# Licensed under the Eclipse Public License, Version 1.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#        http://www.eclipse.org/legal/epl-v10.html
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
# either express or implied. See the License for the specific
# language governing permissions and limitations under the
# License.
#
################################################################
#
# This script takes as input a YAML file describing
# a list of ucli command blocks.
#
# It outputs a C array of ucli_block_t structures representing
# the input data.
#
# This is a simple and cheap mechanism for embedding UCLI sequences
# in the code.
#
################################################################
import yaml
import sys
import datetime

datafilename = sys.argv[1]
datafile = open(datafilename, "r");
data = yaml.load(datafile);
datafile.close()
array_name = sys.argv[2]


print "/******************************************************************************"
print " *"
print " * This file autogenerated from %s on %s." % (datafilename, datetime.datetime.now())
print " *"
print " * DO NOT EDIT. Changes to this file will be discarded upon regeneration."
print " * "
print "******************************************************************************/"
print "#include <uCli/ucli.h>"

print "ucli_block_t %s[] = " % array_name
print "{"

for block in data:
    for (name, commands) in block.iteritems():
        print "  {"
        print "    \"%s\", " % name
        print "    {"
        for command in commands:
            print "      \"%s\", " % command;
        print "    },"
        print "  },"

print "  { (void*)0 }"
print "};"

print "int %s_count = sizeof(%s)/sizeof(%s[0]); " % (array_name, array_name, array_name)
print




