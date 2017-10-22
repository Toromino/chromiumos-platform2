#!/usr/bin/env python2
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The unit test suite for the CrosConfigHost CLI tool."""

from __future__ import print_function

import os
import subprocess
import sys
import unittest

from libcros_config_host import fdt_util

CLI_FILE = './cros_config_host.py'
DTS_FILE = './libcros_config/test.dts'


class CrosConfigHostTest(unittest.TestCase):
  """The unit test suite for the CrosConfigHost CLI tool."""
  def setUp(self):
    path = os.path.join(os.path.dirname(__file__), DTS_FILE)
    (self.dtb_file, self.temp_file) = fdt_util.EnsureCompiled(path)

  def tearDown(self):
    if self.temp_file is not None:
      os.remove(self.temp_file.name)

  def CheckManyLinesWithoutSpaces(self, output, lines=3):
    # Expect there to be a few lines
    self.assertGreater(len(output.split()), lines)
    # Expect each line to not have spaces in it
    for line in output.split():
      self.assertFalse(' ' in line)
      self.assertNotEqual(line[-1:], ' ')
    # Expect the last thing in the output to be a newline
    self.assertEqual(output[-1:], os.linesep)

  def CheckManyLines(self, output, lines=3):
    # Expect there to be a few lines
    self.assertGreater(len(output.split()), lines)
    # Expect each line to not end in space
    for line in output.split():
      self.assertNotEqual(line[-1:], ' ')
    # Expect the last thing in the output to be a newline
    self.assertEqual(output[-1:], os.linesep)


  def testReadStdin(self):
    call_args = '{} -c - list-models < {}'.format(CLI_FILE, self.dtb_file)
    output = subprocess.check_output(call_args, shell=True)
    self.CheckManyLinesWithoutSpaces(output)

  def testListModels(self):
    call_args = '{} -c {} list-models'.format(CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.CheckManyLinesWithoutSpaces(output)

  def testListModelsInvalid(self):
    call_args = '{} -c invalid.dtb list-models'.format(CLI_FILE).split()
    with open(os.devnull, 'w') as devnull:
      with self.assertRaises(subprocess.CalledProcessError):
        subprocess.check_call(call_args, stdout=devnull, stderr=devnull)

  def testGetPropSingle(self):
    call_args = '{} -c {} --model=pyro get / wallpaper'.format(
        CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.assertEqual(output, 'default' + os.linesep)

  def testGetPropSingleWrongModel(self):
    call_args = '{} -c {} --model=dne get / wallpaper'.format(
        CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.assertEqual(output, '')

  def testGetPropSingleWrongPath(self):
    call_args = '{} -c {} --model=pyro get /dne wallpaper'.format(
        CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.assertEqual(output, os.linesep)

  def testGetPropSingleWrongProp(self):
    call_args = '{} -c {} --model=pyro get / dne'.format(
        CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.assertEqual(output, os.linesep)

  def testGetPropAllModels(self):
    call_args = '{} -c {} --all-models get / wallpaper'.format(
        CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.assertEqual(
        output,
        'default{ls}{ls}epic{ls}{ls}{ls}shark{ls}more_shark{ls}'.
        format(ls=os.linesep))

  def testGetFirmwareUris(self):
    call_args = '{} -c {} --model=pyro get-firmware-uris'.format(
        CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.CheckManyLines(output)

  def testGetTouchFirmwareFiles(self):
    call_args = '{} -c {} get-touch-firmware-files'.format(
        CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.CheckManyLines(output, 10)

  def testGetAudioFiles(self):
    call_args = '{} -c {} get-audio-files'.format(
        CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.CheckManyLines(output, 10)

  def testGetFirmwareBuildTargets(self):
    call_args = '{} -c {} get-firmware-build-targets coreboot'.format(
        CLI_FILE, self.dtb_file).split()
    output = subprocess.check_output(call_args)
    self.CheckManyLines(output, 1)

if __name__ == '__main__':
  unittest.main()
