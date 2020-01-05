# -*- coding: utf-8 -*-
"""
Created on Fri Jan  3 21:31:14 2020

@author: aleksey
"""
import gzip

def encodeHtmlToGzip(htmlFile):
    with open(htmlFile,'rb') as fp:
        file_content    = fp.read()
    with gzip.open(htmlFile,'wb') as fzip:
        fzip.write(file_content)

def encodeGzToCFile(gzFile1,cFile,opt):
    with open(gzFile1+'.html','rb') as fgz:
        file_content    = fgz.read()
    with open(cFile,opt) as fc:
        #//File: index_ov2640.html.gz, Size: 4316
        fc.write('//File %s.html.gz, Size:%d\n' % (gzFile1,len(file_content)))
        fc.write('#define %s_html_gz_len %d\n' % (gzFile1,len(file_content)))
        #const uint8_t index_ov2640_html_gz[] = {
        fc.write('const uint8_t %s_html_gz[] = {\n' % (gzFile1))
        bCount = 0
        for bv in file_content:
            fc.write('0x%02x, ' % (bv) )
            bCount +=1
            if bCount==16:
                fc.write('\n')
                bCount = 0
        fc.write('\n};\n')
        

outIndexFile    = 'camera_index2.h'
index2640       = 'index_ov2640' 
index3660       = 'index_ov3660' 

encodeHtmlToGzip( index2640+'.html' )
encodeHtmlToGzip( index3660+'.html' )
encodeGzToCFile(index2640,outIndexFile,'wt')
encodeGzToCFile(index3660,outIndexFile,'at')
