#!/usr/bin/env python
import re
import sys
import time
fp=list();
simu_all=list();
fp.append(open('/Users/lianqiw/work/programming/aos/maos/parms.h','r'));
fp.append(open('/Users/lianqiw/work/programming/aos/maos/types.h','r'));
structs=dict()
for fpi in fp:
    ln=fpi.read();
    fpi.close();
    while True:
        start=ln.find('#')
        end=ln.find('\n', start)
        if start==-1 or end==-1:
            break
        ln=ln[0:start]+ln[end+1:]

    while True:
        start=ln.find('/*')
        end=ln.find('*/', start)
        if start==-1 or end==-1:
            break
        ln=ln[0:start]+ln[end+2:]
        ln=ln.replace('\n','')

    ln=ln.replace('*','* ') #add space after *
    ln=re.sub(r'[ ]*[*]','*', ln) #remove space before *
    ln=re.sub(r'[ ]+',' ', ln) #remove double space
    end=0
    while True:
        struct=ln.find('struct', end)
        if struct==-1:
            break
        struct=struct+len('struct ')
        start=ln.find('{', struct)
        name=ln[struct:start];
        end=ln.find('}', start)
        structs[name]=dict()
        fields=ln[start+1:end].split(';')
        for fi in fields:
            tmp=fi.replace('struct','').replace('const','').lstrip().rstrip().split(' ');
            if len(tmp)==2:
                structs[name][tmp[1]]=tmp[0]
simu=dict()
simu_type=dict()
simu=structs['SIM_T']
rdone=dict()
todo=list()
rdone['SIM_T']=1
for level in range(5):
    todo.append(list())
todo[0].append(simu)
def replace_struct(struct, level):
    for key in struct:
        val0=struct[key]
        val=val0;

        if val[-1]=='*':
            val=val[0:-1];
            ispointer=1
        else:
            ispointer=0

        if rdone.has_key(val):
            pass #avoid repeat
        elif structs.has_key(val):
            rdone[val]=1
            struct[key]=[val0, structs[val]]
            if type(structs[val])==type(dict()): #other types
                todo[level].append(struct[key][1])
        else:
            struct[key]=val0

for level in range(5):
    if len(todo)<=level:
        continue
    for struct in todo[level]:
        replace_struct(struct, level+1)


def var2mx(mexname, cname, ctype):
    out=""
    ans=1
    if ctype[-1]=='*':
        ccast=''
        ctype=ctype[0:-1]
        if ctype[-2:]=='_t':
            ctype=ctype[0:-2];
        if ctype=='map':
            ctype='dmat'
            ccast='(const dmat*)'
        if ctype=='dmat' or ctype=='cmat':
            fun_c=ctype[0]
        elif ctype=='loc' or ctype=='dcell' or ctype=='ccell':
            fun_c=ctype
        else:
            fun_c=''
        if len(fun_c)>0:
            return mexname+"="+fun_c+'2mx('+ccast+cname+');'
        else:
            print "//unknown1 type "+cname+":"+ctype+'*'
            return ''
    else:
        if ctype=='int' or ctype=='double':
            return mexname+'=mxCreateDoubleScalar('+cname+');'
        else:
            print "//unknown2 type "+cname+":"+ctype
            return ''

fundefs=dict()
funheaders=dict()
funcalls=dict()
def struct2mx(vartype, nvar, funprefix, parentpointer, fullpointer, varname, struct):
    if vartype[-1]=='*':
        ispointer=1
    else:
        ispointer=0
        vartype=vartype+"*"

    if varname=="powfs":
        if parentpointer=="simu->":
            nvar="parms->npowfs"
        else:
            nvar="npowfs"
    elif varname=="wfs":
        if parentpointer=="simu->":
            nvar="parms->nwfs"
        else:
            nvar="nwfs"
    elif varname=="wfsr":
        if parentpointer=="simu->":
            nvar="parms->nwfsr"
        else:
            nvar="nwfsr"
    else:
        nvar="1"
    if nvar=="1":
        childpointer=varname+'->'
    else:
        childpointer=varname+'[i_n].'
    if funprefix=="simu_":
        funname=varname
    else:
        funname=funprefix+varname;
    if nvar!="1":
        funheader="static mxArray *get_"+funname+"(const "+vartype+" "+varname+", int nvar);";
    else:
        funheader="static mxArray *get_"+funname+"(const "+vartype+" "+varname+");";
    fundef=funheader[0:-1]+"{\n"
    fundef+="\tif(!"+varname+") return mxCreateDoubleMatrix(0,0,mxREAL);\n"
    if nvar!="1":
        fundef+="\tmxArray* tmp=mxCreateStructMatrix(nvar,1,0,0);\n\tint i;\n\tmxArray *tmp2;\n"
    else:
        fundef+="\tmxArray* tmp=mxCreateStructMatrix(1,1,0,0);\n\tint i;\n\tmxArray *tmp2;\n"
    if nvar!="1":
        fundef+="for(int i_n=0; i_n<nvar; i_n++){\n"
        stind='i_n'
    else:
        stind='0'
    for key in struct: 
        valtype=struct[key]
        ans=""
        if type(valtype)==type(list()):
            if key=='save':
                continue
            ans+="\ttmp2="+struct2mx(valtype[0], nvar, funname+"_", childpointer, fullpointer+childpointer, key, valtype[1])
        else:
            ans+=var2mx("\ttmp2", childpointer+key, valtype)
        if len(ans)>0:
            fundef+=ans;
            fundef+="i=mxAddField(tmp, \""+key+"\");"
            fundef+="mxSetFieldByNumber(tmp, "+stind+", i, tmp2);\n"
    if nvar!="1":
        fundef+="}\n"
    fundef+="\treturn tmp;\n}\n"
    fundefs[funname]=fundef
    funheaders[funname]=funheader
    if funname.replace('simu_','').find('_')==-1:
        if ispointer==0:
            funcalls[funname]="get_"+funname+"(&("+fullpointer+varname+"));"
        elif nvar!="1":
            funcalls[funname]="get_"+funname+"("+fullpointer+varname+", "+fullpointer+nvar+");"
        else:
            funcalls[funname]="get_"+funname+"("+fullpointer+varname+");"

    if ispointer==0:
        return "get_"+funname+"(&("+parentpointer+varname+"));"
    elif nvar!="1":
        return "get_"+funname+"("+parentpointer+varname+", "+parentpointer+nvar+");"
    else:
        return "get_"+funname+"("+parentpointer+varname+");"

struct2mx("SIM_T*", "1", "","","", "simu", simu)

if len(sys.argv)>1:
    fnout=sys.argv[1]
else:
    fnout='../mex/maos2mex.h'
fc=open(fnout,'w')
keys=funheaders.keys()
keys.sort()
for key in keys:
    print>>fc, funheaders[key]
keys=fundefs.keys()
keys.sort()
for key in keys:
    print>>fc, fundefs[key]
print>>fc, "static void get_data_help(){\n",
for key in funcalls:
    print>>fc,"\tinfo2(\""+key+"=maos('get','"+key+"')\\n\");"
print>>fc,"}\n"
print>>fc, "static mxArray *get_data(SIM_T *simu, char *key){\n\t",
for key in funcalls:
    print>>fc, "if(!strcmp(key, \""+key.replace("simu_","")+"\")) return "+funcalls[key]+"\n\telse",
print>>fc,"{get_data_help();return mxCreateDoubleMatrix(0,0,mxREAL);}\n}"
fc.close()