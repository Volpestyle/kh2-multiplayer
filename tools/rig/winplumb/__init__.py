"""Shared Windows packet plumbing. Import creates no process or connection.

The caller retains ownership, save/consent/STOP and deadline enforcement.
"""
from .text import read_text,read_json,write_text,write_json,encoded_command,utf8_env

