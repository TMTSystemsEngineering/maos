#!/usr/bin/env python3

#2019-07-18 First version

#Convert functions defined in aolib.h to aoslib.py that is callable by Python
#derived from lib2mex.py

#from __future__ import print_function
import sys
import maos
from pathlib import Path
import json
import keyword
if len(sys.argv)>2:
    srcdir=sys.argv[1];
    fnout=sys.argv[2];
else:
    srcdir=srcdir=str(Path.home())+'/work/programming/aos'
    fnout=srcdir+'/scripts/libaos.py'

simu_all=list();

headlist=['lib/accphi.h','lib/cn2est.h','lib/kalman.h', 'lib/locfft.h','lib/muv.h','lib/servo.h','lib/stfun.h','lib/turbulence.h',
          'lib/mkdtf.h','lib/hyst.h', 'math/chol.h','sys/scheduler.h']
structs=maos.parse_structs(srcdir, headlist)

#funcs=maos.parse_func(srcdir, structs, ['lib/aolib.h'])

funcs=maos.parse_func(srcdir, structs, ['mex/aolib.h', 'lib/accphi.h','lib/cn2est.h','lib/kalman.h', 'lib/locfft.h','lib/muv.h','lib/servo.h','lib/stfun.h','lib/turbulence.h', 'lib/mkdtf.h', 'math/chol.h'])
fpout=open(fnout,'w')
print("#!/usr/bin/env python", file=fpout)
print("\n#Do not modify! This file is automatically generated by lib2py.py.\n", file=fpout)
print("from interface import *", file=fpout)

#map between MAOS C types and python types
aotype2py={
    'dmat':'cell',
    'lmat':'cell',
    'cmat':'cell',
    'smat':'cell',
    'zmat':'cell',
    'cell':'cell',
    'map_t':'cell',
    'loc_t':'loc',
    'pts_t':'loc',
    'dsp':'csc',
    'csp':'csc',
    'ssp':'csc',
    'zsp':'csc',
    'char':'c_char',
    'double':'c_double',
    'float':'c_float',
    'real':'c_double',
    'size_t':'c_size_t',
    'long':'c_long',
    'int':'c_int',
    'uint32_t':'c_uint',
    'uint64_t':'c_ulonglong',
    'void':'',
}
#process variable type
def handle_type(argtype, argname):
    if (len(argtype)>3 and argtype[-3:]=='***') or argname.count('[')>0: #known
        return ('Unknown','Unknown','Unknown')
    elif argtype[-2:]=='**': #output
        isout=1
        isref=1
        argtype=argtype[:-2]
    elif argtype=='real*' or  argtype=='double*':
        isout=2
        isref=1
        argtype=argtype[:-1]
    elif argtype[-1:]=='*': #pointer
        isout=0
        isref=1
        argtype=argtype[:-1]
    else:
        isout=0
        isref=0

    if argtype[-4:]=='cell':
        argtype='cell'

    pytype=aotype2py.get(argtype, None)

    if pytype is None: #represent as a struct
        if structs.get(argtype,None) :
            pytype='make_class(\''+argtype+'\''+','+json.dumps(structs.get(argtype,None))+')'
        else:
            #print(argtype, argname)
            pytype='Unknown'
            argname='Unknown'
        py2c='py2cellref('+argname+')'
    elif pytype=='cell':#arbitrary array
        if isref:
            py2c='py2cellref('+argname+')'
        else:
            py2c='py2cell('+argname+')'
    else:
        py2c=pytype+'('+argname+')'
        if isref: #pointer input
            if pytype=='loc':
                py2c='byref('+py2c+')'
            elif pytype=='c_double' or pytype=='c_float':
                py2c='byref('+argname+')'
            elif pytype=='c_char':
                py2c='c_char_p('+argname+'.encode("utf-8"))'
            else:
                py2c=argname+'.ctypes.data_as(c_void_p)'


    if isout==1: #output pointer
        prep1=argname+'=POINTER('+pytype+')()'
        prep2='pt2py('+argname+')'
    elif isout==2:
        prep1=argname+'='+pytype+'()'
        prep2=argname+'.value'
    else:
        prep1=''
        prep2=''
    return (py2c, prep1, prep2)

#process function return type
def handle_output(funtype, funname):
    if funtype[-1:]=='*': #pointer
        ispointer=1;
        funtype=funtype[:-1]
    else:
        ispointer=0
    if funtype[-4:]=='cell':
        funtype='cell'
    fun_arg=aotype2py.get(funtype, None)
    if fun_arg is None:#represent as a struct
        if structs.get(funtype,None):
            fun_arg='make_class(\''+funtype+'\''+','+json.dumps(structs.get(funtype,None))+')'
        else:
            print(funtype, funname)
            fun_arg='Unknown'
    if ispointer:
        fun_val='pt2py(out)'
        fun_arg='POINTER('+fun_arg+')'
    else:
        fun_val='out'
    if len(fun_arg)==0:
        fun_arg='None'
    return ('lib.'+funname+'.restype='+fun_arg, fun_val)

funcalls=list()
for funname in funcs: #loop over functions
    funtype=funcs[funname][0]  #function return type
    funargs=funcs[funname][1]  #Function arguments
    funname2=funcs[funname][2] #C function name
    if len(funargs)==0 :
        continue
    fundef=''
    fundef_free=''
    pointer_output=''
    funout=''
   
    #Define Python function 
    pyargin='' #python function definition arguments
    argin=''   #arguments passing to C function
    pyargout=''  #output from python function
    prepout=''
    if funtype!='void': #with C outupt
        fun_arg, fun_val=handle_output(funtype, funname2)
        pyargout+=fun_val+','
        argout='out='
    else:
        argout=''

    for arg in funargs: #loop over the arguments
        argtype=arg[0]
        argname=arg[1]

        if argname in ['loc','input','in','out','str','string','len','format','type','min','max']:
            argname += '_'
        if argtype=='number': #literal numbers
            argin+=argname+','
        else: #variables
            py2c, prep1, prep2=handle_type(argtype, argname)
            argin+=py2c+','
            #TODO: some argument maybe both input and output argument.
            if prep1: #output argument
                prepout+='    '+prep1+'\n'
                pyargout+=prep2+','
                if len(funargs)==1: #when only 1 argument, assume is also input.
                    pyargin+=argname+','
            else: #python function input argument
                pyargin+=argname+','

    if pyargin[-1]==',':
        pyargin=pyargin[0:-1]
    if argin[-1]==',':
        argin=argin[0:-1]
    if len(pyargout)>0 and pyargout[-1]==',':
        pyargout=pyargout[0:-1]
    if len(prepout)>0 and prepout[-1]=='\n':
        prepout=prepout[0:-1]
    fundef+='def '+funname+'('+pyargin+'):'   #def function 
    if funtype!='void':
        fundef+='\n    '+fun_arg          #C function return type
    if len(prepout)>0:
        fundef+='\n'+prepout          #C function arguments
    fundef+='\n    '+argout+'lib.'+funname2+'('+argin+')'
    if len(pyargout)>0:
        fundef+='\n    return '+pyargout
        
    if(fundef.find('Unknown'))>-1:
        print("'''", file=fpout)
    print(fundef, file=fpout);
    if(fundef.find('Unknown'))>-1:
        print("'''", file=fpout)
fpout.close()
