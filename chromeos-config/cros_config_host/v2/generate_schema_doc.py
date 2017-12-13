#!/usr/bin/env python2
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generates markdown from the JSON schema."""

from __future__ import print_function

import argparse
import collections
import json
import os.path
import sys


def ParseArgs(argv):
  """Parse the available arguments.

  Invalid arguments or -h cause this function to print a message and exit.

  Args:
    argv: List of string arguments (excluding program name / argv[0])

  Returns:
    argparse.Namespace object containing the attributes.
  """
  parser = argparse.ArgumentParser(
      description='Generates Markdown documentation based on the config schema')
  parser.add_argument(
      '-s',
      '--schema',
      default='cros_config_schema.json',
      type=str,
      help='Schema file that is processed')
  parser.add_argument(
      '-o',
      '--output',
      type=str,
      help='Output file that will be generated')
  return parser.parse_args(argv)


def PopulateTypeDef(name, type_def, ref_types, output):
  """Populates type definitions in the output (recursive)

  Args:
    name: Name of the type
    type_def: Dict containing all of the type def attributes
    ref_types: Shared type definitions using the #ref attribute
    output: Running array of markdown string output lines
  """
  attrs = collections.OrderedDict(
      sorted(type_def.get('properties', {}).items()))
  child_types = collections.OrderedDict()
  output.append('### %s' % name)
  output.append(
      '| Attribute | Type   | RegEx     | Required | Description |')
  output.append(
      '| --------- | ------ | --------- | -------- | ----------- |')
  for attr in attrs:
    attr_name = attr
    type_attrs = attrs[attr]
    if '$ref' in type_attrs:
      type_attrs = ref_types[type_attrs['$ref']]

    attr_type = type_attrs['type']
    regex = type_attrs.get('pattern', '')
    if regex:
      # Regex need escaping for markdown
      regex = '```%s```' % regex
    description = type_attrs.get('description', '')
    required_list = type_def.get('required', [])
    required = attr in required_list
    if type_attrs['type'] == 'object':
      child_types[attr_name] = type_attrs
      attr_type = '[%s](#%s)' % (attr_name, attr_name)

    output_tuple = (attr_name, attr_type, regex, required, description)
    output.append('| %s | %s | %s | %s | %s |' % output_tuple)

  output.append('')
  for child_type in child_types:
    PopulateTypeDef(child_type, child_types[child_type], ref_types, output)


def Main(schema, output):
  """Generates markdown documentation based on the JSON schema.

  Args:
    schema: Schema file.
    output: Output file.
  """
  with open(schema, 'r') as schema_stream:
    schema_json = json.loads(schema_stream.read())
    ref_types = {}
    for type_def in schema_json['typeDefs']:
      ref_types[
          '#/typeDefs/%s' %
          type_def] = schema_json['typeDefs'][type_def]

    type_def_outputs = []
    type_def_outputs.append('[](begin_definitions)')
    type_def_outputs.append('')
    type_def_outputs.append('## CrOS Config Type Definitions (v2)')
    PopulateTypeDef(
        'model',
        schema_json['properties']['models']['items'],
        ref_types,
        type_def_outputs)
    type_def_outputs.append('')
    type_def_outputs.append('[](end_definitions)')
    type_def_outputs.append('')

    if output:
      pre_lines = []
      post_lines = []

      if os.path.isfile(output):
        with open(output) as output_stream:
          output_lines = output_stream.readlines()
          pre_section = True
          post_section = False
          for line in output_lines:
            if 'begin_definitions' in line:
              pre_section = False

            if pre_section:
              pre_lines.append(line)

            if post_section:
              post_lines.append(line)

            if 'end_definitions' in line:
              post_section = True

      with open(output, 'w') as output_stream:
        if pre_lines:
          output_stream.writelines(pre_lines)

        output_stream.write('\n'.join(type_def_outputs))

        if post_lines:
          output_stream.writelines(post_lines)
    else:
      print('\n'.join(type_def_outputs))

if __name__ == "__main__":
  args = ParseArgs(sys.argv[1:])
  Main(args.schema, args.output)
