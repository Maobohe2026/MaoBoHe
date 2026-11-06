# vc6fix3.py —— 处理嵌套 for 的版本
# 把所有 for (int X = ...) 的循环变量改成唯一名（从后往前替换，避免嵌套互相干扰）
import re
import sys


def match_brace(text, i):
    depth = 0
    n = len(text)
    in_s = None
    while i < n:
        c = text[i]
        if in_s:
            if c == '\\':
                i += 2
                continue
            if c == in_s:
                in_s = None
        elif c in '"\'':
            in_s = c
        elif c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        elif c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return n


def collect(text):
    pat = re.compile(r'for\s*\(\s*int\s+([A-Za-z_]\w*)\s*=')
    out = []
    for m in pat.finditer(text):
        name = m.group(1)
        i = m.end()
        depth = 1
        while i < len(text) and depth:
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
            i += 1
        j = i
        while j < len(text) and text[j] in ' \t\r\n':
            j += 1
        if j < len(text) and text[j] == '{':
            body_end = match_brace(text, j)
        else:
            k = text.find(';', j)
            body_end = len(text) if k < 0 else k + 1
        out.append((m.start(), m.end(), body_end, name))
    return out


def transform(text):
    spans = collect(text)
    counter = 0
    for start, head_end, body_end, name in reversed(spans):
        counter += 1
        newname = '%s_r%d' % (name, counter)
        seg = text[start:body_end]
        seg = re.sub(r'\b%s\b' % re.escape(name), newname, seg)
        text = text[:start] + seg + text[body_end:]
    return text, len(spans)


def fix_suffixes(text):
    text = re.sub(r'\blong\s+long\b', '__int64', text)
    text = re.sub(r'\b(0[xX][0-9a-fA-F]+)[uU]?[lL]{2}\b', r'\1ui64', text)
    text = re.sub(r'\b(\d+)[uU]?[lL]{2}\b', r'\1ui64', text)
    return text


def fix_noop(text):
    return re.sub(r'#define\s+(\w+)\(\.\.\.\)\s*\(\(void\)0\)',
                  r'static void \1(const char *fmt, ...) { (void)fmt; }', text)


def main(files):
    for path in files:
        raw = open(path, 'rb').read()
        try:
            src = raw.decode('utf-8')
            enc = 'utf-8'
        except UnicodeDecodeError:
            src = raw.decode('gbk')
            enc = 'gbk'
        src = fix_noop(src)
        src = fix_suffixes(src)
        src, n = transform(src)
        open(path, 'wb').write(src.encode(enc))
        print('%-16s loops renamed: %d' % (path.split('\\')[-1], n))


if __name__ == '__main__':
    main(sys.argv[1:])
