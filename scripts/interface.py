#!/Usr/bin/env python

#Use ctypes to interface with C libraries
#POINTER is the class type of pointer
#pointer() acts on actual array, while POINTER() works on class type.
#pointer(cell(arr)) #creates cell Structure for np.array type arr and makes a pointer
#pcell=POINTER(cell) ; pcell() #Creates a class for cell Structure and then makes an object pointer with no content.

#c_int is a ctypes type
#a=c_int(42) #creates a ctypes int object
#p=pointer(a) creates C compatible pointers to object a
#p.contents retreates the contents of pointer p
#addressof(p) retreates the address of p
#use pd=cast(p, POINTER(c_double)) to convert pointer p to c_double pointer 
#use pd=cast(address, POINTER(c_double)) to convert address in c_double pointer
#cast(addressof(a), POINTER(c_double)).contents #reinterpreted int a as double

#pointer() creates a real pointer to a ctype object (Sturecture)
#byref() is a simplified version of pointer(). It cannot be used as byref(byref())
#can use byref(pointer())
#Set restype to return correct value
#Set argtypes for type checking for input into C code
#be careful regarding memory management.


#TODO: investigate whether CFFI is a better solution.
import os
#import sys
#from pdb import set_trace as keyboard
from ctypes import *
#import json
import numpy as np
import scipy.sparse as sp
#from copy import deepcopy
#from warnings import warn
aolib_so=os.environ.get('MAOS_AOLIB', 'aolib.so')
try:
    lib=cdll.LoadLibrary(aolib_so)
except:
    raise Exception('Load aolib.so failed from '+aolib_so)
from readbin import set_header, convert_output

def simplify(arr, do_stack=1, do_squeeze=0):
    '''convert object array a[i,j][k,n]... to simple ndarray a[i,j,k,n,...]
        shape and singleton dimensions are preserved.
    '''
    if type(arr) is list:
        if len(arr)==1:
            arr=arr[0]
        else:
            arr=np.array(arr)
    if type(arr) is np.ndarray and do_squeeze:
        arr=np.squeeze(arr)
        if arr.size==1:
            return arr.item(0) #this works for 0d array
    if type(arr) is np.ndarray and arr.dtype.name=='object':
        flags=np.zeros(arr.shape,dtype=bool)
        can_stack=0
        for ind,iarr in np.ndenumerate(arr):
            arr[ind]=simplify(iarr,do_stack,do_squeeze)
            if arr[ind].size>0:
                flags[ind]=True
                if arr[ind].dtype.name!='object':
                    can_stack=1
                    shape=arr.shape+arr[ind].shape
        if do_stack and can_stack:
            try:
                arr=np.stack(arr[flags])
                arr.shape=shape
                if do_squeeze:
                    arr=np.queeze(arr)
            except Exception as err:
                #print('stack failed', err)
                pass
    return arr

#obtain ctypes type information from MAOS id.
#The value is (type, complex?, kind(0:dense, 1: sparse, 2: loc, 10: cell))
id2ctype={
    25600: (c_double,1,1), #M_CSP64
    25601: (c_double,0,1), #M_SP64
    25602: (c_double,0,0), #'M_DBL'
    25603: (c_long,  0,0), #'M_INT64'
    25604: (c_double,1,0), #'M_CMP'
    25605: (c_int,   0,0), #'M_INT32',),
    25606: (c_float, 1,1), #'M_CSP32',),
    25607: (c_float, 0,1), #'M_SP32',),
    25608: (c_float, 0,0), #'M_FLT',),
    25609: (c_float, 1,0), #'M_ZMP',),
    25610: (c_char,  0,0), #'M_INT8',),
    25611: (c_short, 0,0), # 'M_INT16',),
    25633: (c_void_p,0,10),#MC_ANY
    222210: (c_double,0,2),#M_LOC64
}

def pt2py(pointer):
    '''convert C array pointer to numpy array. Freeing C memory'''
    headers=[]
    headers.clear()
    if bool(pointer):
        out=pointer.contents.as_array(headers)
        pointer.contents.free()
    else:
        out=np.array([])
    if len(headers)==1:
        headers=headers[0]
    set_header(headers)
    return convert_output(out)
#def pt2py_header(pointer):
#    return (pt2py(pointer), deepcopy(headers))
#convert C vector to numpy array. Memory is copied.
def as_array(arr, id, shape):
    ''' convert C array arr to numpy.array based on id'''
    (tt, iscomplex, issparse)=id2ctype.get(id)
    if tt is None:
        print("id2ctype: unknown type:", id)
        return np.array([])
    elif not bool(arr) or shape[0]==0:
        return np.array([])
    else:
        parr=cast(arr, POINTER(tt))
        if iscomplex:
            nparr=np.ctypeslib.as_array(parr, shape=(*shape,2))
            nparr2=nparr[...,0]+1j*nparr[...,1]
        else:
            nparr=np.ctypeslib.as_array(parr, shape=shape)
            nparr2=np.copy(nparr)
        return nparr2

def py2cell(arr, tid=0):
    '''convert numpy.array to C array adaptively (based on its type)'''
    if type(arr) is list:
        arr=np.asarray(arr)
    if sp.issparse(arr):
        return csr(arr, tid)
    else:
        return cell(arr, tid)

def py2cellref(arr, tid=0):
    '''convert numpy array to C array pointer adaptively or output'''
    if arr is None:
        return None
    elif type(arr) is list:
        arr = np.asarray(arr)
    if sp.issparse(arr):
        return byref(csr(arr,tid))
    elif type(arr) is np.ndarray:        
        if arr.size==0:
            return None #turn empty ndarray to Null pointer. do not use 0
        else:
            return byref(cell(arr,tid))
    else:
        return byref(arr)

def arr2object(arr): 
    '''convert numpy number arrays with more than 3 dimensions to numpy object arrays'''
    if type(arr) is not np.ndarray:
        print('Unsupported data type')
        return arr
    if arr.ndim<=2:
        return arr
    ndim=2-(arr.ndim%2)
    arr2=np.empty((arr.shape[0:ndim]),dtype=object)
    if ndim==2:
        for i in range(arr2.shape[0]):
            for j in range(arr2.shape[1]):
                arr2[i,j]=arr2object(arr[i,j])
    else:
        for i in range(arr2.shape[0]):
            arr2[i]=arr2object(arr[i])
    return arr2

class cell(Structure):
    '''To interface numpy.array with C cell '''
    _fields_ = [ #fields compatible with C type of cell and mat
        ('id', c_uint32),
        ('p',  c_void_p),
        ('nx', c_long),
        ('ny', c_long),
        ('header', c_char_p),
        ('dummy_fp', c_void_p),
        ('dummy_fft', c_void_p),
        ('dummy_1', c_void_p),
        ('dummy_2', c_void_p),
        ('dummy_3', c_void_p),
        ('dummy_4', c_void_p),
        ]
 
    def __init__(self, arr=None, tid=0):#convert from numpy to C. Memory is borrowed
        #attributes set within __init__ are per object
        dtype2id={#Conversion from numpy dtype to maos id
            np.double:  0x6402,
            np.int64:   0x6403,
            np.complex128:0x6404,
            np.int32:   0x6405,
            np.float32: 0x6408,
            np.complex64:0x6409,
            np.int8:    0x640A,
            np.int16:   0x640B,
            np.object_: 0x6421
        }
        if type(arr) is list:
            arr=np.asarray(arr)
        if arr.ndim>2:
            arr=arr2object(arr)
        if arr is not None:
            tmpid=dtype2id.get(arr.dtype.type)
            if tmpid is None:
                print("init: Unknown data" +str( arr.dtype.type))
                return None
            if tid!=0 and tmpid != tid and tid!=0x6421 : #and tmpid !=0x6421:
                dtp = next(key for key, value in dtype2id.items() if value == tid) 
                try: #convert data
                    arr=arr.astype(dtp)
                    tmpid=tid
                except:
                    raise(Exception('data mismatch want {}, got {}'.format(tmpid, tid)))
            self.id=tmpid

            if arr.ndim>0:
                self.nx=arr.shape[-1]
            if arr.ndim>1:
                self.ny=arr.shape[-2]
            else:
                if self.nx>0:
                    self.ny=1
                else:
                    self.ny=0
            if self.nx==0:
                self.p=0
            elif arr.dtype.kind != 'O':
                if arr.flags['C']==False:
                    arr=arr.copy(order='C')
                self.p=arr.ctypes.data_as(c_void_p)
            else:
                self.qarr=np.zeros(self.shape(1), dtype=object) #stores objects
                self.parr=np.zeros(self.shape(1), dtype=c_void_p) #store pointer of objects
                for iy in range(self.ny):
                    for ix in range(self.nx):
                        if arr.ndim==1:
                            arri=arr[ix]
                        else:
                            arri=arr[iy,ix]
                        if arri is not None:
                            self.qarr[iy,ix]=py2cell(arri) #object array
                            self.parr[iy,ix]=addressof(self.qarr[iy,ix]) #object pointer array
                        else:
                            self.parr[iy,ix]=0
                self.p=self.parr.ctypes.data_as(c_void_p)
            self.arr=arr #keep a reference to arr as it can be a copy otherwise it may be destroyed
        else:
            self.id=25633
            self.p=None
            self.nx=0
            self.ny=0
        self.header=None
        self.dummy_fp=None
        self.dummy_fft=None
        self.dummy_mem=None
        self.dummy_async=None
        
    def shape(self, twod):
        if self.ny > 1 or twod:
            return (self.ny, self.nx)
        else:
            return (self.nx,) #last , is necessary

    def as_array(self, headers): #convert form C to numpy. Memory is copied
        try:
            (tt, iscomplex, kind)=id2ctype.get(self.id)
        except:
            print("id2ctype: unknown type", id);
            kind=-1
        if kind==0: #dense matrix
            if self.header:
                headers.append(self.header.decode('ascii'))
            return as_array(self.p, self.id, self.shape(0))
        elif kind==1: #sparse matrix
            return cast(addressof(self), POINTER(csr)).contents.as_array(headers)
        elif kind==2: #loc
            return cast(addressof(self), POINTER(loc)).contents.as_array(headers)
        elif kind==10: #cell
            res=np.empty(self.shape(1), dtype=object)
            parr=cast(self.p, POINTER(c_void_p))
            headerc=[]
            if self.header:
                headerc.append(self.header.decode('ascii'))
            for iy in range(self.ny):
                for ix in range(self.nx):
                    address=parr[ix+self.nx*iy]
                    if address is not None:
                        pp=cast(int(address), POINTER(cell))
                        res[iy, ix]=pp.contents.as_array(headerc) #recursive
                    else:
                        res[iy, ix]=np.array([])
            if self.ny==1:
                res=res[0,]
            if len(headerc)==1:
                headerc=headerc[0]
            if headerc:
                headers.append(headerc)
            return simplify(res)
        else:
            print('as_array: Unknown data, id='+ str(self.id))
            return np.array([],dtype=object)
    def free(self):
        lib.cellfree_do(byref(self)) #will fail if memory is not allocated by C
        
class loc(Structure):
    '''To interface numpy.array with C lob '''
    _fields_ = [
        ('id',   c_uint32),
        ('locx', c_void_p),
        ('nloc', c_long),
        ('two',  c_long), 
        ('header',c_void_p),
        ('dummy_fp',c_void_p),
        ('dummy_fft',c_void_p),
        ('dummy_1',c_void_p),
        ('dummy_2',c_void_p),
        ('dummy_3',c_void_p),
        ('dummy_4',c_void_p), #up to here. follow cell memory layout.
        ('locy', c_void_p),
        ('locstat_t', c_void_p),
        ('map',  c_void_p),
        ('dx',   c_double),
        ('dy',   c_double),
        ('ht',   c_double),
        ('iac',  c_double),
        ('npad', c_int),
        ]
    def __init__(self, arr=None): #convert from numpy to C. Memory is borrowed
        self.id= 222210 #0x036402 #M_LOC64
        if arr is not None:
            if len(arr.shape)!=2 or arr.shape[0] !=2 :
                raise(Exception('Array has to be of shape 2xn'))
            else:
                if arr.flags['C']==False:
                    arr=self.arr=arr.copy()
                if arr.dtype!=np.double:
                    arr=arr.astype(np.double)
                self.nloc=arr.shape[1]
                self.two=2;
                self.locx=arr[0,].ctypes.data_as(c_void_p)
                self.locy=arr[1,].ctypes.data_as(c_void_p)
                dlocx=np.abs(arr[0,1:]-arr[0,0:-1])
                self.dx=min(dlocx[dlocx>0])
                dlocy=np.abs(arr[1,1:]-arr[1,0:-1])
                self.dy=min(dlocy[dlocy>0])
                #print('loc: dx={0}, dy={1}'.format(self.dx, self.dy))
        #default initialization to zero
    def as_array(self, headers): #convert form C to numpy. Memory is copied
        headers.append(self.header)
        if(self.locx):
            if self.id!=222210:
                raise(Exception('Wrong type'))
            else:
                return as_array(self.locx, 25602, shape=(2, self.nloc))
        else:
            print("locs is empty: ", self.locx)
            raise(Exception("loc is empty"))
    def free(self):
        lib.cellfree_do(byref(self))

class csr(Structure):#CSR sparse matrix. We convert C CSC to Python CSR just like arrays as numpy is row order
    '''To interface numpy.array with C sparse array '''
    _fields_=[ #need to match C memory layout
        ('id', c_uint32),
        ('x', c_void_p),
        ('nx', c_long),
        ('ny', c_long),
        ('header',   c_char_p),
        ('dummy_fp', c_void_p),
        ('nzmax', c_long),
        ('p', c_void_p),
        ('i', c_void_p),
        ('nref', c_void_p),
    ]
    def __init__(self, arr=None, tid=0): #convert from numpy to C. Memory is borrowed
        spdtype2id={#Conversion from sparse type to maos id
            np.float32:   0x6407,
            np.float64:   0x6401,
            np.complex64: 0x6406,
            np.complex128:0x6400,
        }
        if arr is None:
            self.id=spdtype2id.get(np.float64)
        elif sp.issparse(arr):
            if arr.dtype.type in (np.int32, np.int64):
                arr=arr.astype(np.float64)
            self.id=spdtype2id.get(arr.dtype.type)
            if  tid !=0 and tid !=0x6421 and self.id != tid:
                raise(Exception('data mismatch want {}, got {}'.format(self.id, tid)))
            if not arr.format in ('csr','csc'):
                print('Sparse format {} is not supported; will convert to csr'.format(arr.format))
                arr=arr.tocsr()
            if arr.format=='csr':
                self.ny, self.nx=arr.shape #python csr is maos csc transposed
            elif arr.format=='csc':
                self.nx, self.ny=arr.shape 
            #save subarrays to avoid being GC'ed.
            self.xp=arr.data
            self.ip=arr.indices.astype(np.long)
            self.pp=arr.indptr.astype(np.long) #p
            self.x=self.xp.ctypes.data_as(c_void_p) #data
            self.i=self.ip.ctypes.data_as(c_void_p) #row index
            self.p=self.pp.ctypes.data_as(c_void_p)
            self.nzmax=self.pp[-1]
        else:
            raise(Exception('Invalid conversion to csr'))

    def as_array(self, headers): #convert form C to numpy. Memory is copied
        headers.append(self.header)
        if self.nzmax>0:
            self.xp=as_array(self.x, self.id, (self.nzmax,))
            self.ip=as_array(self.i, 25603, (self.nzmax,))
            self.pp=as_array(self.p, 25603, (self.ny+1,))
            return sp.csr_array((self.xp, self.ip, self.pp), shape=(self.ny, self.nx))
        else:
            return sp.csr_array((self.nx,self.ny))
    def free(self):
        lib.cellfree_do(byref(self))

def convert_fields(fields):
    '''convert a C type keyword to ctypes type'''
    val2type={
        '*':c_void_p,
        'double':c_double,
        'real':c_double,
        'float':c_float,
        'long':c_long,
        'int':c_int,
    }
    newfields=[]
    for key,val in fields.items():
        if val[-1]=='*':
            val=c_void_p
        else:
            val=val2type[val]
        newfields.append((key,val))
    return newfields

def make_class(name, fields):
    '''Dynaically create a ctypes class with field listed '''
    newfields=convert_fields(fields)
    class newclass(Structure):
        pass
        def as_array(self):#convert struct into dictionary
            out=dict()
            for ff in self._fields_:
                #convert C pointers to POINTER then to array
                field=eval("self.{}".format(ff[0]))
                if field is not None:
                    if ff[1] is c_void_p:
                        out[ff[0]]=cast(field,POINTER(cell)).contents.as_array()
                        #exec('out[\''+ff[0]+'\']=cast(self.'+ff[0]+',POINTER(cell)).contents.as_array()')
                    else:
                        out[ff[0]]=field
                else:
                    out[ff[0]]=None
            out['struct']=self
            return out
        def free(self):
            print('to implement: free');
    newclass._fields_=newfields
    return newclass
