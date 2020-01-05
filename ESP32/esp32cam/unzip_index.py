import gzip

def extractGzipFromCFile(cFile,gzipFile):
    state       = 0
    zipData     = bytearray()
    with open(cFile) as fp:
       line     = fp.readline()
       cnt      = 1
       while line:
           #print("Line {}: {}".format(cnt, line.strip()))
           if state == 0:
               if line.find(gzipFile+'_html_gz[]') != -1:
                   state = 1
           else:
               if line.find('};') != -1:
                   state = 0
               else:
                   strList = line.split(',')
                   strList = [item for item in strList if item.find('x')!=-1]
                   for sbyte in strList:
                       zipData.append(int(sbyte,16))
           line = fp.readline()
           cnt += 1
           
    with open(gzipFile+'.gzip','wb') as fp:
        fp.write(zipData)
        
def extractHtmlFromGzip(gzipFile,htmlFile):
    with gzip.open(gzipFile,'rb') as fzip:
        file_content    = fzip.read()
    with open(htmlFile,'wb') as fp:
        fp.write(file_content)

inpIndexFile    = 'camera_index.h'
outIndexFile    = 'camera_index2.h'
index2640       = 'index_ov2640' 
index3660       = 'index_ov3660' 
    
extractGzipFromCFile('camera_index.h','index_ov2640')
extractGzipFromCFile('camera_index.h','index_ov3660')
extractHtmlFromGzip(index2640+'.gzip',index2640+'.html')
extractHtmlFromGzip(index3660+'.gzip',index3660+'.html')

