import re, sys

CODE = {'→': '->', '←': '<-', '–': '-', '—': '-', '…': '...', '×': 'x', '≤': '<=', '≥': '>=', '§': 'section ',
        '−': '-', 'µ': 'u', '↑': '^', '·': '*', '▼': 'v', '‖': '||', '≈': '~', '⁹': '^9', '◄': '<', '►': '>',
        '▲': '^', '▸': '>', '²': '^2', '³': '^3', '¹': '^1', '⁸': '^8', '⁻': '^-', '≠': '!=', '±': '+/-', 'Σ': 'sum',
        '÷': '/', '∈': 'in', '›': '>', '─': '-', '│': '|', '├': '+', '└': '+', '┌': '+', '┐': '+', '┘': '+',
        '┬': '+', '┴': '+', '┼': '+'}

def code_ascii(t):
    for k, v in CODE.items(): t = t.replace(k, v)
    return t

def prose_ascii(t):
    t = re.sub(r'^(#+ Part \d+) — ', r'\1: ', t)
    t = re.sub(r'(?<=\|) *— *(?=\|)', ' - ', t)
    t = re.sub(r' ?— ?', ', ', t)
    t = re.sub(r' – ', ', ', t)
    t = re.sub(r' *→ *', ' to ', t)
    t = re.sub(r' *← *', ' from ', t)
    t = re.sub(r'≤ ?', 'at most ', t)
    t = re.sub(r'≥ ?', 'at least ', t)
    t = re.sub(r' ?≈ ?', ' about ', t)
    t = re.sub(r' ?≠ ?', ' not equal to ', t)
    t = re.sub(r'§ ?', 'section ', t)
    t = t.replace(' × ', ' x ').replace('↑ ', '')
    return code_ascii(t)

def clean_line(l):
    parts = re.split(r'(`[^`]*`)', l)
    out = []
    for i, p in enumerate(parts):
        if i % 2: out.append(code_ascii(p))
        else:
            p = p.replace('**', '')
            out.append(prose_ascii(p))
    return ''.join(out).rstrip()

LABEL = {'NOTE': 'Note', 'IMPORTANT': 'Important', 'WARNING': 'Warning', 'TIP': 'Tip', 'CAUTION': 'Warning'}

def clean(md):
    lines = md.split('\n'); out = []; fence = False; i = 0
    while i < len(lines):
        l = lines[i]
        if l.lstrip().startswith('```'):
            fence = not fence; out.append(l); i += 1; continue
        if fence:
            out.append(code_ascii(l)); i += 1; continue
        if l.startswith('>'):
            block = []
            while i < len(lines) and lines[i].startswith('>'):
                block.append(re.sub(r'^> ?', '', lines[i])); i += 1
            label = None
            m = re.match(r'^\[!(\w+)\]\s*$', block[0]) if block else None
            if m: label = LABEL.get(m.group(1).upper(), 'Note'); block = block[1:]
            text = ' '.join(b.strip() for b in block if b.strip())
            if label: text = f'{label}: {text}'
            if out and out[-1].strip(): out.append('')
            out.append(clean_line(text)); out.append('')
            continue
        out.append(clean_line(l)); i += 1
    s = '\n'.join(out)
    s = re.sub(r'\n{3,}', '\n\n', s)
    return s

if __name__ == '__main__':
    for p in sys.argv[1:]:
        s = open(p).read(); open(p, 'w').write(clean(s))
