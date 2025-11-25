#!/usr/bin/env python3
# Ersetzt alle Emojis durch ASCII-Symbole

import re

replacements = {
    '▶️': '[>]',
    '▶': '[>]',
    '⏸️': '[||]',
    '⏸': '[||]',
    '⏹️': '[#]',
    '⏹': '[#]',
    '🔄': '[R]',
    '✅': '[OK]',
    '❌': '[X]',
    '☑️': '[+]',
    '☑': '[+]',
    '☐': '[ ]',
    '📤': '[^]',
    '🎵': '[~]',
    '⭐': '[*]',
    '🗑️': '[DEL]',
    '🔧': '[FIX]',
    '🚀': '[>>]',
    '⚠️': '[!]',
    '⚠': '[!]',
    '💾': '[SAVE]',
    '⊘': '[STOP]',
}

def fix_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    original = content
    for emoji, ascii_sym in replacements.items():
        content = content.replace(emoji, ascii_sym)
    
    if content != original:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"Fixed: {filepath}")
        return True
    return False

if __name__ == '__main__':
    import sys
    if len(sys.argv) > 1:
        fix_file(sys.argv[1])
    else:
        print("Usage: python3 fix_emojis.py <file>")
