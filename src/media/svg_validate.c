/* Explicit rendering subset checked against PlutoSVG fd8a080's render dispatch
 * and attribute consumers. clipPath is recognized upstream but not rendered.
 * Unsupported semantics are errors; metadata/title/desc subtrees are ignored. */
#include "limits.h"
#include <expat.h>
#include <plutovg.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define MAX_NODES 16384
#define MAX_REFERENCES 65536
enum { PAINT_INHERIT, PAINT_NONE, PAINT_ACTIVE };
typedef struct Node { char *id; int child,sibling,first_ref,state; unsigned expansions,render_depth; const char *tag; int fill,stroke; bool partial_opacity; } Node;
typedef struct Reference { char *id; int next,target; bool paint; } Reference;
typedef struct Validation {
    XML_Parser parser; PPError *error; bool failed; unsigned depth,ignored;
    int stack[128],count,ref_count; Node *nodes;Reference *refs;
} Validation;
static const char *const tags[]={"svg","g","defs","symbol","use","path","rect","circle","ellipse","line","polygon","polyline","linearGradient","radialGradient","stop",NULL};
static const char *const attrs[]={"id","x","y","width","height","viewBox","preserveAspectRatio","d","points","cx","cy","r","rx","ry","x1","y1","x2","y2","fx","fy","transform","gradientTransform","gradientUnits","spreadMethod","offset","color","display","visibility","fill","fill-opacity","fill-rule","opacity","stop-color","stop-opacity","stroke","stroke-width","stroke-opacity","stroke-linecap","stroke-linejoin","stroke-miterlimit","stroke-dasharray","stroke-dashoffset",NULL};
static const char *const shapes[]={"path","rect","circle","ellipse","line","polygon","polyline",NULL};
static const char *listed(const char *name,const char *const *list) { for(;*list;list++) if(!strcmp(name,*list)) return *list;return NULL; }
static void fail(Validation *v,int code,const char *reason) { if(!v->failed) pp_media_error(v->error,code,reason);v->failed=true;XML_StopParser(v->parser,XML_FALSE); }
static char *copy(const char *s,size_t n) { char *p=malloc(n+1);if(p) { memcpy(p,s,n);p[n]=0; }return p; }
static void reference(Validation *v,int node,const char *id,size_t length,bool paint) {
    if(!length || length>256 || v->ref_count==MAX_REFERENCES) { fail(v,PP_ERROR_RESOURCE_LIMIT,"SVG reference limit exceeded");return; }
    Reference *r=&v->refs[v->ref_count]; r->id=copy(id,length);if(!r->id) { fail(v,PP_ERROR_MEMORY,"Could not allocate SVG reference");return; }
    r->next=v->nodes[node].first_ref;r->target=-1;r->paint=paint;v->nodes[node].first_ref=v->ref_count++;
}
typedef struct Value { const char *data; size_t length; } Value;
static Value trimmed(const char *s) {
    while(isspace((unsigned char)*s)) s++;
    size_t n=strlen(s);while(n && isspace((unsigned char)s[n-1])) n--;
    return (Value){s,n};
}
static bool keyword(Value value,const char *word) { return value.length==strlen(word) && !memcmp(value.data,word,value.length); }
static bool color_value(Value value) {
    if(keyword(value,"currentColor")) return true;
    plutovg_color_t color;
    return value.length && plutovg_color_parse(&color,value.data,(int)value.length)==(int)value.length;
}
/* Numeric accumulation adapted from PlutoSVG parse_float(), revision
 * fd8a080b3d0bccc41f1ae708d3785e7f08398a58, source/plutosvg.c.
 * Float intermediates are intentional: accepting a finite strtod result could
 * accept syntax/value pairs that the renderer rejects after float overflow.
 *
 * Copyright (c) 2020-2026 Samuel Ugochukwu <sammycageagle@gmail.com>
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
static bool decimal(Value value,double *number,size_t *consumed) {
    size_t p=0,digits=0;float integer=0,fraction=0,exponent=0;int sign=1,exponent_sign=1;
    if(p<value.length && (value.data[p]=='+' || value.data[p]=='-')) { if(value.data[p]=='-') sign=-1;p++; }
    while(p<value.length && isdigit((unsigned char)value.data[p])) { integer=10.f*integer+(value.data[p]-'0');p++;digits++; }
    if(p<value.length && value.data[p]=='.') {
        p++;size_t start=p;float divisor=1;
        while(p<value.length && isdigit((unsigned char)value.data[p])) { fraction=10.f*fraction+(value.data[p]-'0');divisor*=10.f;p++;digits++; }
        if(start==p) return false;
        fraction/=divisor;
    }
    if(!digits) return false;
    if(p+1<value.length && (value.data[p]=='e' || value.data[p]=='E') && value.data[p+1]!='x' && value.data[p+1]!='m') {
        p++;if(p<value.length && (value.data[p]=='+' || value.data[p]=='-')) { if(value.data[p]=='-') exponent_sign=-1;p++; }
        size_t start=p;while(p<value.length && isdigit((unsigned char)value.data[p])) { exponent=10.f*exponent+(value.data[p]-'0');p++; }if(start==p) return false;
    }
    float result=sign*(integer+fraction);
    if(exponent) result*=powf(10.f,exponent_sign*exponent);
    if(!isfinite(result) || result < -FLT_MAX || result > FLT_MAX) return false;
    *number=result;*consumed=p;return true;
}
static bool opacity_value(Value value,double *opacity) {
    size_t p;if(!decimal(value,opacity,&p)) return false;
    float result=(float)*opacity;
    if(p<value.length && value.data[p]=='%') { result/=100.f;p++; }
    if(p!=value.length) return false;
    *opacity=fmaxf(0,fminf(1,result));return true;
}
static bool length_value(Value value,bool negative,size_t *consumed) {
    double number;size_t p;if(!decimal(value,&number,&p) || (!negative && number<0)) return false;
    if(p<value.length && value.data[p]=='%') p++;
    else if(p+2<=value.length) {
        Value units={value.data+p,2};
        if(keyword(units,"px") || keyword(units,"pt") || keyword(units,"pc") || keyword(units,"in") || keyword(units,"cm") || keyword(units,"mm")) p+=2;
    }
    *consumed=p;return true;
}
static bool dash_array(Value value) {
    if(keyword(value,"none") || keyword(value,"inherit")) return true;
    size_t p=0,count=0;
    while(p<value.length) {
        size_t n;if(++count>128 || !length_value((Value){value.data+p,value.length-p},false,&n)) return false;p+=n;
        if(p==value.length) return true;
        size_t boundary=p;while(p<value.length && isspace((unsigned char)value.data[p])) p++;
        if(p<value.length && value.data[p]==',') { p++;while(p<value.length && isspace((unsigned char)value.data[p])) p++;if(p==value.length) return false; }
        if(p==boundary) return false;
    }
    return false;
}
static void paint_value(Validation *v,int node,const char *name,Value value) {
    Node *n=&v->nodes[node];int kind=PAINT_ACTIVE;
    if(keyword(value,"none")) kind=PAINT_NONE;
    else if(keyword(value,"inherit")) kind=PAINT_INHERIT;
    else if(value.length>=3 && !memcmp(value.data,"url",3)) {
        size_t p=3;while(p<value.length && isspace((unsigned char)value.data[p])) p++;
        if(p==value.length || value.data[p++]!='(') goto unsupported;
        while(p<value.length && isspace((unsigned char)value.data[p])) p++;
        if(p==value.length || value.data[p++]!='#') goto unsupported;
        size_t start=p;while(p<value.length && value.data[p]!=')') { if(isspace((unsigned char)value.data[p])) goto unsupported;p++; }
        if(p==value.length || p==start) goto unsupported;
        size_t id_length=p++-start;
        while(p<value.length && isspace((unsigned char)value.data[p])) p++;
        if(p<value.length && !color_value((Value){value.data+p,value.length-p})) goto unsupported;
        reference(v,node,value.data+start,id_length,true);
    } else if(!color_value(value)) goto unsupported;
    if(!strcmp(name,"fill")) n->fill=kind;else n->stroke=kind;
    return;
unsupported:
    fail(v,PP_ERROR_FORMAT,"Unsupported SVG paint value; use a supported color, none, inherit, or local gradient");
}
static void attribute(Validation *v,int node,const char *name,const char *value) {
    if(!strcmp(name,"xmlns")) { if(strcmp(value,"http://www.w3.org/2000/svg")) fail(v,PP_ERROR_FORMAT,"Unsupported SVG namespace");return; }
    if(!strcmp(name,"xmlns:xlink")) { if(strcmp(value,"http://www.w3.org/1999/xlink")) fail(v,PP_ERROR_FORMAT,"Unsupported SVG link namespace");return; }
    if(!strcmp(name,"version") || !strcmp(name,"xml:space") || !strncmp(name,"data-",5)) return;
    if(!strcmp(name,"href") || !strcmp(name,"xlink:href")) {
        const char *tag=v->nodes[node].tag;
        if(strcmp(tag,"use") && strcmp(tag,"linearGradient") && strcmp(tag,"radialGradient")) { fail(v,PP_ERROR_FORMAT,"SVG reference on unsupported element");return; }
        if(value[0]!='#' || !value[1]) { fail(v,PP_ERROR_FORMAT,"SVG references must be local fragment IDs");return; }
        reference(v,node,value+1,strlen(value+1),false);return;
    }
    /* CSS is deliberately outside this explicit subset, avoiding silently
     * ignored properties and selector/cascade differences in the renderer. */
    if(!listed(name,attrs)) { char message[256];snprintf(message,sizeof(message),"Unsupported SVG attribute: %s",name);fail(v,PP_ERROR_FORMAT,message);return; }
    if(!strcmp(name,"id")) {
        if(!*value || strlen(value)>256) { fail(v,PP_ERROR_RESOURCE_LIMIT,"SVG ID exceeds 256 bytes");return; }
        for(int i=0;i<node;i++) if(v->nodes[i].id && !strcmp(v->nodes[i].id,value)) { fail(v,PP_ERROR_FORMAT,"Duplicate SVG ID");return; }
        v->nodes[node].id=copy(value,strlen(value));if(!v->nodes[node].id) fail(v,PP_ERROR_MEMORY,"Could not allocate SVG ID");return;
    }
    Value parsed=trimmed(value);
    if(!strcmp(name,"fill") || !strcmp(name,"stroke")) { paint_value(v,node,name,parsed);return; }
    if(!strcmp(name,"color") || !strcmp(name,"stop-color")) {
        if(!color_value(parsed) && !(strcmp(name,"stop-color") && keyword(parsed,"inherit"))) fail(v,PP_ERROR_FORMAT,"Unsupported SVG color value");
        return;
    }
    if(!strcmp(name,"opacity") || !strcmp(name,"fill-opacity") || !strcmp(name,"stroke-opacity") || !strcmp(name,"stop-opacity")) {
        if((!strcmp(name,"fill-opacity") || !strcmp(name,"stroke-opacity")) && keyword(parsed,"inherit")) return;
        double opacity;if(!opacity_value(parsed,&opacity)) { fail(v,PP_ERROR_FORMAT,"Unsupported SVG opacity value");return; }
        if(!strcmp(name,"opacity") && opacity>0 && opacity<1) {
            if(!listed(v->nodes[node].tag,shapes)) { fail(v,PP_ERROR_FORMAT,"Fractional SVG container/use opacity requires unsupported group compositing");return; }
            v->nodes[node].partial_opacity=true;
        }
        return;
    }
    if(!strcmp(name,"fill-rule") || !strcmp(name,"stroke-linecap") || !strcmp(name,"stroke-linejoin")) {
        bool valid=keyword(parsed,"inherit");
        if(!strcmp(name,"fill-rule")) valid=valid || keyword(parsed,"nonzero") || keyword(parsed,"evenodd");
        else if(!strcmp(name,"stroke-linecap")) valid=valid || keyword(parsed,"butt") || keyword(parsed,"round") || keyword(parsed,"square");
        else valid=valid || keyword(parsed,"miter") || keyword(parsed,"round") || keyword(parsed,"bevel");
        if(!valid) fail(v,PP_ERROR_FORMAT,"Unsupported SVG fill rule or stroke cap/join value");return;
    }
    if(!strcmp(name,"stroke-width") || !strcmp(name,"stroke-dashoffset")) {
        size_t n;if(!keyword(parsed,"inherit") && (!length_value(parsed,!strcmp(name,"stroke-dashoffset"),&n) || n!=parsed.length)) fail(v,PP_ERROR_FORMAT,"Unsupported SVG stroke length value");return;
    }
    if(!strcmp(name,"stroke-dasharray")) { if(!dash_array(parsed)) fail(v,PP_ERROR_FORMAT,"Unsupported SVG dash array or more than 128 dashes");return; }
    if(!strcmp(name,"stroke-miterlimit")) {
        double number;size_t n;if(!keyword(parsed,"inherit") && (!decimal(parsed,&number,&n) || n!=parsed.length || number<1)) fail(v,PP_ERROR_FORMAT,"Unsupported SVG stroke miter limit");return;
    }
    if(strstr(value,"var(") || strstr(value,"!important")) fail(v,PP_ERROR_FORMAT,"Unsupported SVG CSS painting value");
}
static void XMLCALL start(void *context,const char *name,const char **attributes) {
    Validation *v=context;if(++v->depth>128) { fail(v,PP_ERROR_RESOURCE_LIMIT,"SVG nesting exceeds 128");return; }
    if(!strcmp(name,"script")) { fail(v,PP_ERROR_FORMAT,"SVG scripts are forbidden");return; }
    if(v->ignored) return;
    if(!strcmp(name,"metadata") || !strcmp(name,"title") || !strcmp(name,"desc")) { v->ignored=v->depth;return; }
    const char *tag=listed(name,tags);
    if(!tag || (v->depth==1 && strcmp(name,"svg"))) { fail(v,PP_ERROR_FORMAT,"Unsupported SVG element");return; }
    if(v->count==MAX_NODES) { fail(v,PP_ERROR_RESOURCE_LIMIT,"SVG exceeds 16384 elements");return; }
    int index=v->count++;Node *node=&v->nodes[index];node->tag=tag;node->child=node->sibling=node->first_ref=-1;v->stack[v->depth-1]=index;
    if(v->depth>1) { Node *parent=&v->nodes[v->stack[v->depth-2]];node->sibling=parent->child;parent->child=index; }
    /* PlutoSVG keeps raw attribute strings and does not decode XML entities.
     * Refuse rendering attributes containing character references so validation
     * and rendering cannot interpret different IDs, paint, or geometry. */
    int offset,size;const char *input=XML_GetInputContext(v->parser,&offset,&size);
    if(input) { const char *p=input+offset,*end=input+size;char quote=0;for(;p<end;p++) { if(quote) { if(*p=='&') { fail(v,PP_ERROR_FORMAT,"SVG rendering attributes cannot contain XML character references");return; }if(*p==quote) quote=0; }else { if(*p=='\'' || *p=='"') quote=*p;else if(*p=='>') break; } } }
    for(int i=0;attributes[i] && !v->failed;i+=2) attribute(v,index,attributes[i],attributes[i+1]);
}
static void XMLCALL end(void *context,const char *name) { Validation *v=context;(void)name;if(v->depth==v->ignored) v->ignored=0;v->depth--; }
static void XMLCALL text_data(void *context,const char *s,int length) { Validation *v=context;if(v->ignored) return;for(int i=0;i<length;i++) if(!isspace((unsigned char)s[i])) { fail(v,PP_ERROR_FORMAT,"SVG text content is unsupported");return; } }
static void XMLCALL doctype(void *context,const char *name,const char *system,const char *public_id,int internal) { (void)name;(void)system;(void)public_id;(void)internal;fail(context,PP_ERROR_FORMAT,"SVG DTD and entities are forbidden"); }
static int XMLCALL external(XML_Parser parser,const char *context,const char *base,const char *system,const char *public_id) { (void)context;(void)base;(void)system;(void)public_id;fail(XML_GetUserData(parser),PP_ERROR_FORMAT,"External SVG entities are forbidden");return XML_STATUS_ERROR; }
static void XMLCALL processing(void *context,const char *target,const char *data) { (void)target;(void)data;fail(context,PP_ERROR_FORMAT,"SVG processing instructions are unsupported"); }
static bool visit(Validation *v,int i,unsigned depth) {
    if(depth>128) { fail(v,PP_ERROR_RESOURCE_LIMIT,"SVG reference graph exceeds 128 render levels");return false; }
    Node *n=&v->nodes[i];if(n->state==2) return true;if(n->state==1) { fail(v,PP_ERROR_FORMAT,"Cyclic SVG local references");return false; }n->state=1;n->render_depth=1;
    for(int child=n->child;child>=0;child=v->nodes[child].sibling) { if(!visit(v,child,depth+1)) return false;Node *c=&v->nodes[child];n->expansions+=c->expansions;if(n->render_depth<c->render_depth+1) n->render_depth=c->render_depth+1; }
    for(int r=n->first_ref;r>=0;r=v->refs[r].next) { Reference *ref=&v->refs[r];if(!visit(v,ref->target,depth+1)) return false;Node *target=&v->nodes[ref->target];n->expansions+=1+target->expansions;if(n->render_depth<target->render_depth+1) n->render_depth=target->render_depth+1; }
    if(n->expansions>256 || n->render_depth>128) { fail(v,PP_ERROR_RESOURCE_LIMIT,"SVG local expansion exceeds 256 references or 128 render levels");return false; }n->state=2;return true;
}
/* Only fill/stroke presence is propagated, following render_use's temporary
 * parent replacement. This is a rejection check, not a second style renderer. */
static bool check_opacity(Validation *v,int i,int fill,int stroke,unsigned depth) {
    if(depth>128) { fail(v,PP_ERROR_RESOURCE_LIMIT,"SVG opacity context exceeds 128 render levels");return false; }
    Node *n=&v->nodes[i];
    if(n->fill!=PAINT_INHERIT) fill=n->fill;
    if(n->stroke!=PAINT_INHERIT) stroke=n->stroke;
    if(n->partial_opacity && strcmp(n->tag,"line") && fill==PAINT_ACTIVE && stroke==PAINT_ACTIVE) {
        fail(v,PP_ERROR_FORMAT,"Fractional SVG object opacity with both fill and stroke requires unsupported compositing");return false;
    }
    if(!strcmp(n->tag,"use")) {
        for(int r=n->first_ref;r>=0;r=v->refs[r].next) if(!v->refs[r].paint && !check_opacity(v,v->refs[r].target,fill,stroke,depth+1)) return false;
    } else if(!strcmp(n->tag,"svg") || !strcmp(n->tag,"g") || !strcmp(n->tag,"symbol")) {
        for(int child=n->child;child>=0;child=v->nodes[child].sibling) if(!check_opacity(v,child,fill,stroke,depth+1)) return false;
    }
    return true;
}
bool pp_svg_validate(PPBytes b,PPError *e) {
    Validation v={0};v.error=e;v.parser=XML_ParserCreate(NULL);v.nodes=calloc(MAX_NODES,sizeof(*v.nodes));v.refs=calloc(MAX_REFERENCES,sizeof(*v.refs));
    if(!v.parser || !v.nodes || !v.refs) { if(v.parser) XML_ParserFree(v.parser);free(v.nodes);free(v.refs);return pp_media_error(e,PP_ERROR_MEMORY,"Could not allocate SVG validator"); }
    XML_SetUserData(v.parser,&v);XML_SetElementHandler(v.parser,start,end);XML_SetCharacterDataHandler(v.parser,text_data);XML_SetStartDoctypeDeclHandler(v.parser,doctype);XML_SetExternalEntityRefHandler(v.parser,external);XML_SetProcessingInstructionHandler(v.parser,processing);XML_SetParamEntityParsing(v.parser,XML_PARAM_ENTITY_PARSING_NEVER);
    if(XML_Parse(v.parser,(const char *)b.data,(int)b.size,XML_TRUE)!=XML_STATUS_OK && !v.failed) fail(&v,PP_ERROR_FORMAT,"Malformed SVG XML");
    for(int i=0;i<v.count && !v.failed;i++) for(int r=v.nodes[i].first_ref;r>=0 && !v.failed;r=v.refs[r].next) {
        Reference *ref=&v.refs[r];for(int j=0;j<v.count;j++) if(v.nodes[j].id && !strcmp(v.nodes[j].id,ref->id)) { ref->target=j;break; }
        if(ref->target<0) fail(&v,PP_ERROR_FORMAT,"Unresolved SVG local reference");
        else if(ref->paint && strcmp(v.nodes[ref->target].tag,"linearGradient") && strcmp(v.nodes[ref->target].tag,"radialGradient")) fail(&v,PP_ERROR_FORMAT,"SVG paint reference must name a gradient");
        else if(!ref->paint) {
            const char *from=v.nodes[i].tag,*to=v.nodes[ref->target].tag;
            if(!strcmp(from,"use")) {
                if(!strcmp(to,"defs") || !strcmp(to,"linearGradient") || !strcmp(to,"radialGradient") || !strcmp(to,"stop")) fail(&v,PP_ERROR_FORMAT,"SVG use must reference a renderable element");
            } else if(strcmp(to,"linearGradient") && strcmp(to,"radialGradient")) fail(&v,PP_ERROR_FORMAT,"SVG gradient href must name a gradient");
        }
    }
    if(!v.failed && !v.count) fail(&v,PP_ERROR_FORMAT,"SVG has no root");if(!v.failed) visit(&v,0,1);
    if(!v.failed) check_opacity(&v,0,PAINT_ACTIVE,PAINT_NONE,1);
    XML_ParserFree(v.parser);for(int i=0;i<v.count;i++) free(v.nodes[i].id);for(int i=0;i<v.ref_count;i++) free(v.refs[i].id);free(v.nodes);free(v.refs);return !v.failed;
}
