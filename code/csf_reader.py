# 解析红警2的csf文件
import os
import struct
 
def read_dword(f):
    return struct.unpack('<I', f.read(4))[0]
 
 
def write_dword(f, num):
    return f.write(struct.pack('<I', num))
 
 
class CsfHeader(object):
    # 0x0	char[4]  	" FSC"
    identifier = b' FSC'
    langMap = ['US', 'UK', 'German', 'French', 'Spanish', 'Italian',
               'Japanese', 'Jabberwockie', 'Korean', 'Chinese', 'Unknown']
 
    def __init__(self) -> None:
        # 0x4	DWORD       CSF Version
        self.version = 0
        # 0x8	DWORD	    NumLabels
        self.NumLabels = 0
        # 0xC	DWORD       NumStrings
        self.NumStrings = 0
        # 0x10	DWORD       (unused)
        # 0x14	DWORD       Language
        self.Language = 0
 
    def __str__(self):
        s = 'NumLabels= %d, NumStrings= %d, Language= %s' % (
            self.NumLabels, self.NumStrings, CsfHeader.langMap[self.Language])
        return s
 
    def save(self, f):
        f.write(CsfHeader.identifier)
        write_dword(f, self.version)
        write_dword(f, self.NumLabels)
        write_dword(f, self.NumStrings)
        # (unused)
        write_dword(f, 0)
        write_dword(f, self.Language)
        
 
class LabelHeader(object):
    # 0x0	char[4]     " LBL"
    identifier = b' LBL'
 
    def __init__(self) -> None:
        # 0x4	DWORD       Number of string pairs
        self.Number_of_string_pairs = 0
        # 0x8	DWORD       LabelNameLength
        self.LabelNameLength = 0
        # 0xC	char[LabelNameLength]   LabelName
        self.LabelName = ''
 
    def save(self, f):
        f.write(LabelHeader.identifier)
        write_dword(f, self.Number_of_string_pairs)
        write_dword(f, self.LabelNameLength)
        f.write(self.LabelName)
 
class LabelValue(object):
    # 0x0	char[4]         " RTS" or "WRTS"
    identifiers = [b' RTS', b'WRTS']
 
    def __init__(self) -> None:
        # 0x4	DWORD       ValueLength
        self.ValueLength = 0
        # 0x8	byte[ValueLength*2]                         Value
        self.Value = ''
        # 0x8+ValueLength*2	DWORD                           ExtraValueLength
        self.ExtraValueLength = 0
        # 0x8+ValueLength*2+0x4	char[ExtraValueLength]      ExtraValue
        self.ExtraValue = ''
        self.text = ''
 
    def decode(self):
        self.Value = bytes([~x & 0xff for x in self.Value])
        self.text = self.Value.decode('utf16')
        return self.text
 
    def encode(self):
        self.Value = bytes([~x & 0xff for x in self.Value])
        return self.Value
 
    def replace(self, text):
        self.text = text
        # 注意，这里需要指明编码大小端, 只写 'utf16' 会多出两个字节指示编码的大小端
        self.Value = text.encode('utf-16-le')
        self.ValueLength = len(text)
 
    def save(self, f):
        if self.ExtraValueLength == 0:
            f.write(LabelValue.identifiers[0])
        else:
            f.write(LabelValue.identifiers[1])
        write_dword(f, self.ValueLength)
        f.write(self.encode())
        if self.ExtraValueLength != 0:
            write_dword(f, self.ExtraValueLength)
            f.write(self.ExtraValue)
 
 
class Label(object):
    def __init__(self) -> None:
        self.header = LabelHeader()
        self.values = []
 
    def save(self, f):
        self.header.save(f)
        for v in self.values:
            v.save(f)
 
 
# Header
 
 
def parse_csf_header(f):
    f.seek(0, 0)
    if f.read(4) != CsfHeader.identifier:
        print("Parse Csf Header Failed!")
        return False
    header = CsfHeader()
    header.version = read_dword(f)
    header.NumLabels = read_dword(f)
    header.NumStrings = read_dword(f)
    f.read(4)
    header.Language = read_dword(f)
 
    return header
 
 
def parse_label_header(f):
    if f.read(4) != LabelHeader.identifier:
        print("Parse Label Header Failed!")
        return False
    header = LabelHeader()
    header.Number_of_string_pairs = read_dword(f)
    header.LabelNameLength = read_dword(f)
    header.LabelName = f.read(header.LabelNameLength)
 
    return header
 
 
def parse_label_value(f):
    t = f.read(4)
    if t not in LabelValue.identifiers:
        print("Parse Value Failed!")
        return False
    value = LabelValue()
    value.ValueLength = read_dword(f)
    value.Value = f.read(value.ValueLength * 2)
    if t == LabelValue.identifiers[1]:
        value.ExtraValueLength = read_dword(f)
        value.ExtraValue = f.read(value.ExtraValueLength)
 
    value.decode()
 
    return value
 
# Label
 
 
def parse_label(f):
    header = parse_label_header(f)
    if not header:
        print("Parse Label Failed!")
        return False
 
    label = Label()
    label.header = header
    for i in range(header.Number_of_string_pairs):
        value = parse_label_value(f)
        if not value:
            return label
        label.values.append(value)
 
    return label
 
 
def parse_csf(filename):
    f = open(filename, 'rb')
    header = parse_csf_header(f)
    if not header:
        print("Parse Csf Failed!")
        return False
    labels = []
    for i in range(header.NumLabels):
        if i == 1270:
            print("break")
        label = parse_label(f)
        if not label:
            break
        labels.append(label)
 
    f.close()
    return (header, labels)
 
 
def dump_texts(filename: str, labels: Label):
    f = open(filename, 'w', encoding='utf8')
    for l in labels:
        for v in l.values:
            s = '[%s]%s'%(l.header.LabelName.decode(), v.text)
            f.write(s + '\n')
 
    f.close()
 
    return True
 
def read_translate(filename:str):
    translations = {}
    name = ''
    value = ''
    with open(filename, 'r', encoding='utf8') as f:
        while True:
            line = f.readline()
            if line == '':
                break
            if line[0] != '[':
                translations[name] += '\n' + line[:-1]
                continue
            pos = line.index(']')
            name = line[1:pos]
            value = line[pos + 1:-1]
            translations[name] = value
 
    print("translations count: %d"%len(translations))
 
    return translations
 
def replace_translation(labels, translations):
    for l in labels:
        for v in l.values:
            name = l.header.LabelName.decode()
            new_text = translations[name]
            v.replace(new_text)
 
    return labels
 
 
def main():
    filename = r'G:\hongj2yuri\RA2YR\ra2md.csf'
    header, labels = parse_csf(filename)
    print(str(header))
    # 提取繁体字信息
    # df = os.path.splitext(filename)[0] + '.txt'
    # dump_texts(df, labels)
    
    # Word繁体到简体转换，然后新建一个txt文档，保存转换后的结果
 
    # 读取转换后的简体字信息
    translations = read_translate(r'G:\hongj2yuri\RA2YR\ra2md_sc.txt')
    # 转换所有Label中的繁体信息
    labels = replace_translation(labels, translations)
    # 保存新文件
    nf = os.path.splitext(filename)[0] + '_sc.csf'
    with open(nf, 'wb') as f:
        header.save(f)
        for l in labels:
            l.save(f)
 
    return True
 
 
main()