#!/usr/bin/env python2
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Transforms and validates cros config from source YAML to target JSON"""

from __future__ import print_function

import argparse
import collections
import copy
import json
from jsonschema import validate
import os
import re
import sys
import yaml

this_dir = os.path.dirname(__file__)

CHROMEOS = 'chromeos'
MODELS = 'models'
DEVICES = 'devices'
PRODUCTS = 'products'
SKUS = 'skus'
CONFIG = 'config'
BUILD_ONLY_ELEMENTS = [
    '/firmware',
    '/audio/main/card',
    '/audio/main/cras-config-dir',
    '/audio/main/files'
]
CRAS_CONFIG_DIR = '/etc/cras'
TEMPLATE_PATTERN = re.compile("{{([^}]*)}}")

def GetNamedTuple(mapping):
  """Converts a mapping into Named Tuple recursively.

  Args:
    mapping: A mapping object to be converted.

  Returns:
    A named tuple generated from mapping
  """
  if not isinstance(mapping, collections.Mapping):
    return mapping
  new_mapping = {}
  for k, v in mapping.iteritems():
    if type(v) is list:
      new_list = []
      for val in v:
        new_list.append(GetNamedTuple(val))
      new_mapping[k.replace('-', '_').replace('@', '_')] = new_list
    else:
      new_mapping[k.replace('-', '_').replace('@', '_')] = GetNamedTuple(v)
  return collections.namedtuple('Config', new_mapping.iterkeys())(**new_mapping)

def ParseArgs(argv):
  """Parse the available arguments.

  Invalid arguments or -h cause this function to print a message and exit.

  Args:
    argv: List of string arguments (excluding program name / argv[0])

  Returns:
    argparse.Namespace object containing the attributes.
  """
  parser = argparse.ArgumentParser(
      description='Validates a YAML cros-config and transforms it to JSON')
  parser.add_argument(
      '-s',
      '--schema',
      type=str,
      help='Path to the schema file used to validate the config')
  parser.add_argument(
      '-c',
      '--config',
      type=str,
      help='Path to the config file (YAML) that will be validated/transformed')
  parser.add_argument(
      '-o',
      '--output',
      type=str,
      help='Output file that will be generated by the transform (system file)')
  parser.add_argument(
      '-f',
      '--filter',
      type=bool,
      default=False,
      help='Filter build specific elements from the output JSON')
  return parser.parse_args(argv)

def _SetTemplateVars(template_input, template_vars):
  """Builds a map of template variables by walking the input recursively.

  Args:
    template_input: A mapping object to be walked.
    template_vars: A mapping object built up while walking the template_input.
  """
  to_walk = []
  for key, val in template_input.iteritems():
    if isinstance(val, collections.Mapping):
      to_walk.append(val)
    elif type(val) is not list:
      template_vars[key] = val

  # Do this last so all variables from the parent are in scope first.
  for val in to_walk:
    _SetTemplateVars(val, template_vars)


def _ApplyTemplateVars(template_input, template_vars):
  """Evals the input and applies the templating schema using the provided vars.

  Args:
    template_input: Input that will be updated based on the templating schema.
    template_vars: A mapping of all the variables values available.
  """
  to_walk = []
  for key in template_input.keys():
    val = template_input[key]
    if isinstance(val, collections.Mapping):
      to_walk.append(val)
    elif isinstance(val, basestring):
      for template_var in TEMPLATE_PATTERN.findall(val):
        replace_string = "{{%s}}" % template_var
        if template_var not in template_vars:
          raise ValidationError(
              "Referenced template variable '%s' doesn't "
              "exist string '%s'.\nInput: %s\nVariables:%s"
              % (template_var, val, template_input, template_vars))
        var_value = template_vars[template_var]

        # This is an ugly side effect of templating with primitive values.
        # The template is a string, but the target value needs to be int.
        # This is sort of a hack for now, but if the problem gets worse, we
        # can come up with a more scaleable solution.
        #
        # Guessing this problem won't continue though beyond the use of 'sku-id'
        # since that tends to be the only strongly typed value due to its use
        # for identity detection.
        is_int = isinstance(var_value, int)
        if is_int:
          var_value = str(var_value)

        # If the caller only had one value and it was a template variable that
        # was an int, assume the caller wanted the string to be an int.
        if is_int and val == replace_string:
          val = template_vars[template_var]
        else:
          val = val.replace(replace_string, var_value)
      template_input[key] = val

  # Do this last so all variables from the parent are in scope first.
  for value in to_walk:
    _ApplyTemplateVars(value, template_vars)


def _DeleteTemplateOnlyVars(template_input):
  """Deletes all variables starting with $

  Args:
    template_input: Input that will be updated based on the templating schema.
  """
  to_delete = []
  for key in template_input.keys():
    val = template_input[key]
    if isinstance(val, collections.Mapping):
      _DeleteTemplateOnlyVars(val)
    elif key.startswith('$'):
      to_delete.append(key)

  for key in to_delete:
    del template_input[key]


def TransformConfig(config):
  """Transforms the source config (YAML) to the target system format (JSON)

  Applies consistent transforms to covert a source YAML configuration into
  JSON output that will be used on the system by cros_config.

  Args:
    config: Config that will be transformed.

  Returns:
    Resulting JSON output from the transform.
  """
  config_yaml = yaml.load(config)
  json_from_yaml = json.dumps(config_yaml, sort_keys=True, indent=2)
  json_config = json.loads(json_from_yaml)
  configs = []
  if DEVICES in json_config[CHROMEOS]:
    for device in json_config[CHROMEOS][DEVICES]:
      template_vars = {}
      _SetTemplateVars(device, template_vars)
      for product in device[PRODUCTS]:
        _SetTemplateVars(product, template_vars)
        for sku in device[SKUS]:
          _SetTemplateVars(sku, template_vars)
          # Allow variables to template themselves.  For now, only allowing
          # 1 level deep, but could make indefinite if necessary.
          _ApplyTemplateVars(template_vars, template_vars)
          _ApplyTemplateVars(sku, template_vars)
          config = copy.deepcopy(sku[CONFIG])
          _DeleteTemplateOnlyVars(config)
          configs.append(config)
  else:
    configs = json_config[CHROMEOS][MODELS]


  # Drop everything except for models since they were just used as shared
  # config in the source yaml.
  json_config = {CHROMEOS: {MODELS: configs}}

  return json.dumps(json_config, sort_keys=True, indent=2)

def _GetFirmwareUris(model_dict):
  """Returns a list of (string) firmware URIs.

  Generates and returns a list of firmware URIs for this model. These URIs
  can be used to pull down remote firmware packages.

  Returns:
    A list of (string) full firmware URIs, or an empty list on failure.
  """

  model = GetNamedTuple(model_dict)
  fw = model.firmware
  fw_dict = model.firmware._asdict()

  if not getattr(fw, 'bcs_overlay'):
    return []
  bcs_overlay = fw.bcs_overlay.replace('overlay-', '')
  base_model = fw.build_targets.coreboot

  valid_images = [p for n, p in fw_dict.iteritems()
                  if n.endswith('image') and p]
  uri_format = ('gs://chromeos-binaries/HOME/bcs-{bcs}/overlay-{bcs}/'
                'chromeos-base/chromeos-firmware-{base_model}/{fname}')
  return [uri_format.format(bcs=bcs_overlay, model=model.name, fname=fname,
                            base_model=base_model)
          for fname in valid_images]

def FilterBuildElements(config):
  """Removes build only elements from the schema.

  Removes build only elements from the schema in preparation for the platform.

  Args:
    config: Config (transformed) that will be filtered
  """
  json_config = json.loads(config)
  for model in json_config[CHROMEOS][MODELS]:
    _FilterBuildElements(model, "")

  return json.dumps(json_config, sort_keys=True, indent=2)

def _FilterBuildElements(config, path):
  """Recursively checks and removes build only elements.

  Args:
    config: Dict that will be checked.
    path: Path of elements to filter.
  """
  to_delete = []
  for key in config:
    full_path = "%s/%s" % (path, key)
    if full_path in BUILD_ONLY_ELEMENTS:
      to_delete.append(key)
    elif isinstance(config[key], dict):
      _FilterBuildElements(config[key], full_path)

  for key in to_delete:
    config.pop(key)

def ValidateConfigSchema(schema, config):
  """Validates a transformed cros config against the schema specified

  Verifies that the config complies with the schema supplied.

  Args:
    schema: Source schema used to verify the config.
    config: Config (transformed) that will be verified.
  """
  json_config = json.loads(config)
  schema_yaml = yaml.load(schema)
  schema_json_from_yaml = json.dumps(schema_yaml, sort_keys=True, indent=2)
  schema_json = json.loads(schema_json_from_yaml)
  validate(json_config, schema_json)


class ValidationError(Exception):
  """Exception raised for a validation error"""
  pass


def ValidateConfig(config):
  """Validates a transformed cros config for general business rules.

  Performs name uniqueness checks and any other validation that can't be
  easily performed using the schema.

  Args:
    config: Config (transformed) that will be verified.
  """
  json_config = json.loads(config)
  identities = [str(model['identity'])
                for model in json_config['chromeos']['models']]
  if len(identities) != len(set(identities)):
    raise ValidationError("Identities are not unique: %s" % identities)


def Main(schema, config, output, filter_build_details=False):
  """Transforms and validates a cros config file for use on the system

  Applies consistent transforms to covert a source YAML configuration into
  a JSON file that will be used on the system by cros_config.

  Verifies that the file complies with the schema verification rules and
  performs additional verification checks for config consistency.

  Args:
    schema: Schema file used to verify the config.
    config: Config file that will be verified.
    output: Output file that will be generated by the transform.
    filter_build_details: Whether build only details should be filtered or not.
  """
  if not schema:
    schema = os.path.join(this_dir, 'cros_config_schema.yaml')

  with open(config, 'r') as config_stream:
    json_transform = TransformConfig(config_stream.read())
    with open(schema, 'r') as schema_stream:
      ValidateConfigSchema(schema_stream.read(), json_transform)
      ValidateConfig(json_transform)
      if filter_build_details:
        json_transform = FilterBuildElements(json_transform)
    if output:
      with open(output, 'w') as output_stream:
        output_stream.write(json_transform)
    else:
      print (json_transform)

def main(_argv=None):
  """Main program which parses args and runs

  Args:
    _argv: Intended to be the list of arguments to the program, or None to use
        sys.argv (but at present this is unused)
  """
  args = ParseArgs(sys.argv[1:])
  Main(args.schema, args.config, args.output, args.filter)

if __name__ == "__main__":
  main()
