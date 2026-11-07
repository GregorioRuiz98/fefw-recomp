# SPDX-License-Identifier: GPL-3.0-or-later
"""Fire Emblem target for the locally verified update directory 131072.

The numeric directory is not a verified display version. No Pokopia hooks apply.
"""
from nxrecomp.config import GameConfig, ModuleSpec


class Config(GameConfig):
    name = 'fefw'
    title_id = '04007EF01F8AE000'
    build_id = 'DB93B0125F4850601D61C3EDDE012F8EB2ECFE77'
    modules = [
        ModuleSpec('main', 0x7100000000, 'main'),
        ModuleSpec('sdk', 0x7500000000, 'sdk'),
        ModuleSpec('subsdk0', 0x7600000000, 'subsdk0'),
        ModuleSpec('subsdk1', 0x7610000000, 'subsdk1'),
        ModuleSpec('subsdk2', 0x7620000000, 'subsdk2'),
        ModuleSpec('rtld', 0x7700000000, 'rtld'),
    ]
    extra_entries = {'rtld': [0]}
