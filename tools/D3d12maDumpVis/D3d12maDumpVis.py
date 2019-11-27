#
# Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

import argparse
import json
from PIL import Image, ImageDraw, ImageFont


PROGRAM_VERSION = 'D3D12MA Dump Visualization 1.0.0'
IMG_SIZE_X = 1200
IMG_MARGIN = 8
FONT_SIZE = 10
MAP_SIZE = 24
COLOR_TEXT_H1 = (0, 0, 0, 255)
COLOR_TEXT_H2 = (150, 150, 150, 255)
COLOR_OUTLINE = (155, 155, 155, 255)
COLOR_OUTLINE_HARD = (0, 0, 0, 255)
COLOR_GRID_LINE = (224, 224, 224, 255)


argParser = argparse.ArgumentParser(description='Visualization of D3D12 Memory Allocator JSON dump.')
argParser.add_argument('DumpFile', type=argparse.FileType(mode='r', encoding='utf_16_le'), help='Path to source JSON file with memory dump created by D3D12 Memory Allocator library')
argParser.add_argument('-v', '--version', action='version', version=PROGRAM_VERSION)
argParser.add_argument('-o', '--output', required=True, help='Path to destination image file (e.g. PNG)')
args = argParser.parse_args()

data = {}


def ProcessBlock(dstBlockList, iBlockId, objBlock, sAlgorithm):
    iBlockSize = int(objBlock['TotalBytes'])
    arrSuballocs = objBlock['Suballocations']
    dstBlockObj = {'ID': iBlockId, 'Size':iBlockSize, 'Suballocations':[]}
    dstBlockObj['Algorithm'] = sAlgorithm
    for objSuballoc in arrSuballocs:
        dstBlockObj['Suballocations'].append((objSuballoc['Type'], int(objSuballoc['Size']), int(objSuballoc.get('Flags', 0)), int(objSuballoc.get('Layout', 0))))
    dstBlockList.append(dstBlockObj)


def GetDataForHeapType(sHeapType):
    global data
    if sHeapType in data:
        return data[sHeapType]
    else:
        newHeapTypeData = {'CommittedAllocations':[], 'DefaultPoolBlocks':[], 'CustomPools':{}}
        data[sHeapType] = newHeapTypeData
        return newHeapTypeData


# Returns tuple:
# [0] image height : integer
# [1] pixels per byte : float
def CalcParams():
    global data
    iImgSizeY = IMG_MARGIN
    iImgSizeY += FONT_SIZE + IMG_MARGIN # Grid lines legend - sizes
    iMaxBlockSize = 0
    for dictMemType in data.values():
        iImgSizeY += IMG_MARGIN + FONT_SIZE
        lDedicatedAllocations = dictMemType['CommittedAllocations']
        iImgSizeY += len(lDedicatedAllocations) * (IMG_MARGIN * 2 + FONT_SIZE + MAP_SIZE)
        for tDedicatedAlloc in lDedicatedAllocations:
            iMaxBlockSize = max(iMaxBlockSize, tDedicatedAlloc[1])
        lDefaultPoolBlocks = dictMemType['DefaultPoolBlocks']
        iImgSizeY += len(lDefaultPoolBlocks) * (IMG_MARGIN * 2 + FONT_SIZE + MAP_SIZE)
        for objBlock in lDefaultPoolBlocks:
            iMaxBlockSize = max(iMaxBlockSize, objBlock['Size'])
        """
        dCustomPools = dictMemType['CustomPools']
        for lBlocks in dCustomPools.values():
            iImgSizeY += len(lBlocks) * (IMG_MARGIN * 2 + FONT_SIZE + MAP_SIZE)
            for objBlock in lBlocks:
                iMaxBlockSize = max(iMaxBlockSize, objBlock['Size'])
        """
    fPixelsPerByte = (IMG_SIZE_X - IMG_MARGIN * 2) / float(iMaxBlockSize)
    return iImgSizeY, fPixelsPerByte


def TypeToColor(sType, iFlags, iLayout):
    if sType == 'FREE':
        return 220, 220, 220, 255
    elif sType == 'BUFFER':
        return 255, 255, 0, 255 # Yellow
    elif sType == 'TEXTURE2D' or sType == 'TEXTURE1D' or sType == 'TEXTURE3D':
        if iLayout != 0: # D3D12_TEXTURE_LAYOUT_UNKNOWN
            return 0, 255, 0, 255 # Green
        else:
            if (iFlags & 0x2) != 0: # D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                return 246, 128, 255, 255 # Pink
            elif (iFlags & 0x5) != 0: # D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                return 179, 179, 255, 255 # Blue
            elif (iFlags & 0x8) == 0: # Not having D3D12_RESOURCE_FLAG_DENY_SHARED_RESOURCE
                return 0, 255, 255, 255 # Aqua
            else:
                return 183, 255, 255, 255 # Light aqua
    else:
        return 175, 175, 175, 255 # Gray
    assert False
    return 0, 0, 0, 255


def DrawCommittedAllocationBlock(draw, y, tAlloc): 
    global fPixelsPerByte
    iSizeBytes = tAlloc[1]
    iSizePixels = int(iSizeBytes * fPixelsPerByte)
    draw.rectangle([IMG_MARGIN, y, IMG_MARGIN + iSizePixels, y + MAP_SIZE], fill=TypeToColor(tAlloc[0], tAlloc[2], tAlloc[3]), outline=COLOR_OUTLINE)


def DrawBlock(draw, y, objBlock):
    global fPixelsPerByte
    iSizeBytes = objBlock['Size']
    iSizePixels = int(iSizeBytes * fPixelsPerByte)
    draw.rectangle([IMG_MARGIN, y, IMG_MARGIN + iSizePixels, y + MAP_SIZE], fill=TypeToColor('FREE', 0, 0), outline=None)
    iByte = 0
    iX = 0
    iLastHardLineX = -1
    for tSuballoc in objBlock['Suballocations']:
        sType = tSuballoc[0]
        iByteEnd = iByte + tSuballoc[1]
        iXEnd = int(iByteEnd * fPixelsPerByte)
        if sType != 'FREE':
            if iXEnd > iX + 1:
                iFlags = tSuballoc[2]
                iLayout = tSuballoc[3]
                draw.rectangle([IMG_MARGIN + iX, y, IMG_MARGIN + iXEnd, y + MAP_SIZE], fill=TypeToColor(sType, iFlags, iLayout), outline=COLOR_OUTLINE)
                # Hard line was been overwritten by rectangle outline: redraw it.
                if iLastHardLineX == iX:
                    draw.line([IMG_MARGIN + iX, y, IMG_MARGIN + iX, y + MAP_SIZE], fill=COLOR_OUTLINE_HARD)
            else:
                draw.line([IMG_MARGIN + iX, y, IMG_MARGIN + iX, y + MAP_SIZE], fill=COLOR_OUTLINE_HARD)
                iLastHardLineX = iX
        iByte = iByteEnd
        iX = iXEnd


def BytesToStr(iBytes):
    if iBytes < 1024:
        return "%d B" % iBytes
    iBytes /= 1024
    if iBytes < 1024:
        return "%d KB" % iBytes
    iBytes /= 1024
    if iBytes < 1024:
        return "%d MB" % iBytes
    iBytes /= 1024
    return "%d GB" % iBytes


jsonSrc = json.load(args.DumpFile)
objDetailedMap = jsonSrc['DetailedMap']
if 'CommittedAllocations' in objDetailedMap:
    for tType in objDetailedMap['CommittedAllocations'].items():
        sHeapType = tType[0]
        typeData = GetDataForHeapType(sHeapType)
        for objAlloc in tType[1]:
            typeData['CommittedAllocations'].append((objAlloc['Type'], int(objAlloc['Size']), int(objAlloc.get('Flags', 0)), int(objAlloc.get('Layout', 0))))
if 'DefaultPools' in objDetailedMap:
    for tType in objDetailedMap['DefaultPools'].items():
        sHeapType = tType[0]
        typeData = GetDataForHeapType(sHeapType)
        for sBlockId, objBlock in tType[1]['Blocks'].items():
            ProcessBlock(typeData['DefaultPoolBlocks'], int(sBlockId), objBlock, '')
"""
if 'Pools' in jsonSrc:
    objPools = jsonSrc['Pools']
    for sPoolId, objPool in objPools.items():
        iType = int(objPool['MemoryTypeIndex'])
        typeData = GetDataForHeapType(iType)
        objBlocks = objPool['Blocks']
        sAlgorithm = objPool.get('Algorithm', '')
        sName = objPool.get('Name', None)
        if sName:
            sFullName = sPoolId + ' "' + sName + '"'
        else:
            sFullName = sPoolId
        dstBlockArray = []
        typeData['CustomPools'][sFullName] = dstBlockArray
        for sBlockId, objBlock in objBlocks.items():
            ProcessBlock(dstBlockArray, int(sBlockId), objBlock, sAlgorithm)
"""

iImgSizeY, fPixelsPerByte = CalcParams()

img = Image.new('RGB', (IMG_SIZE_X, iImgSizeY), 'white')
draw = ImageDraw.Draw(img)

try:
    font = ImageFont.truetype('segoeuib.ttf')
except:
    font = ImageFont.load_default()

y = IMG_MARGIN

# Draw grid lines
iBytesBetweenGridLines = 32
while iBytesBetweenGridLines * fPixelsPerByte < 64:
    iBytesBetweenGridLines *= 2
iByte = 0
TEXT_MARGIN = 4
while True:
    iX = int(iByte * fPixelsPerByte)
    if iX > IMG_SIZE_X - 2 * IMG_MARGIN:
        break
    draw.line([iX + IMG_MARGIN, 0, iX + IMG_MARGIN, iImgSizeY], fill=COLOR_GRID_LINE)
    if iByte == 0:
        draw.text((iX + IMG_MARGIN + TEXT_MARGIN, y), "0", fill=COLOR_TEXT_H2, font=font)
    else:
        text = BytesToStr(iByte)
        textSize = draw.textsize(text, font=font)
        draw.text((iX + IMG_MARGIN - textSize[0] - TEXT_MARGIN, y), text, fill=COLOR_TEXT_H2, font=font)
    iByte += iBytesBetweenGridLines
y += FONT_SIZE + IMG_MARGIN

# Draw main content
for sHeapType in data.keys():
    dictMemType = data[sHeapType]
    draw.text((IMG_MARGIN, y), sHeapType, fill=COLOR_TEXT_H1, font=font)
    y += FONT_SIZE + IMG_MARGIN
    index = 0
    for tCommittedAlloc in dictMemType['CommittedAllocations']:
        draw.text((IMG_MARGIN, y), "Committed allocation %d" % index, fill=COLOR_TEXT_H2, font=font)
        y += FONT_SIZE + IMG_MARGIN
        DrawCommittedAllocationBlock(draw, y, tCommittedAlloc)
        y += MAP_SIZE + IMG_MARGIN
        index += 1
    for objBlock in dictMemType['DefaultPoolBlocks']:
        draw.text((IMG_MARGIN, y), "Default pool block %d" % objBlock['ID'], fill=COLOR_TEXT_H2, font=font)
        y += FONT_SIZE + IMG_MARGIN
        DrawBlock(draw, y, objBlock)
        y += MAP_SIZE + IMG_MARGIN
    """
    index = 0
    for sPoolName, listPool in dictMemType['CustomPools'].items():
        for objBlock in listPool:
            if 'Algorithm' in objBlock and objBlock['Algorithm']:
                sAlgorithm = ' (Algorithm: %s)' % (objBlock['Algorithm'])
            else:
                sAlgorithm = ''
            draw.text((IMG_MARGIN, y), "Custom pool %s%s block %d" % (sPoolName, sAlgorithm, objBlock['ID']), fill=COLOR_TEXT_H2, font=font)
            y += FONT_SIZE + IMG_MARGIN
            DrawBlock(draw, y, objBlock)
            y += MAP_SIZE + IMG_MARGIN
            index += 1
    """
del draw
img.save(args.output)

"""
Main data structure - variable `data` - is a dictionary. Key is string - heap type ('DEFAULT', 'UPLOAD', or 'READBACK'). Value is dictionary of:
- Fixed key 'CommittedAllocations'. Value is list of tuples, each containing:
    - [0]: Type : string
    - [1]: Size : integer
    - [2]: Flags : integer (0 if unknown)
    - [3]: Layout : integer (0 if unknown)
- Fixed key 'DefaultPoolBlocks'. Value is list of objects, each containing dictionary with:
    - Fixed key 'ID'. Value is int.
    - Fixed key 'Size'. Value is int.
    - Fixed key 'Suballocations'. Value is list of tuples as above.
X- Fixed key 'CustomPools'. Value is dictionary.
  - Key is string with pool ID/name. Value is list of objects representing memory blocks, each containing dictionary with:
    - Fixed key 'ID'. Value is int.
    - Fixed key 'Size'. Value is int.
    - Fixed key 'Algorithm'. Optional. Value is string.
    - Fixed key 'Suballocations'. Value is list of tuples as above.
"""
