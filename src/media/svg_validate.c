/* Explicit rendering subset checked against PlutoSVG fd8a080's render dispatch
 * and attribute consumers. clipPath is recognized upstream but not rendered.
 * Unsupported semantics are errors; metadata/title/desc subtrees are ignored. */
#include "limits.h"
#include <expat.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define MAX_NODES 16384
#define MAX_REFERENCES 65536
typedef struct Node { char *id; int child,sibling,first_ref,state; unsigned expansions,render_depth; const char *tag; } Node;
typedef struct Reference { char *id; int next,target; bool paint; } Reference;
typedef struct Validation {
    XML_Parser parser; PPError *error; bool failed; unsigned depth,ignored;
    int stack[128],count,ref_count; Node *nodes;Reference *refs;
} Validation;
static const char *const tags[]={"svg","g","defs","symbol","use","path","rect","circle","ellipse","line","polygon","polyline","linearGradient","radialGradient","stop",NULL};
static const char *const attrs[]={"id","x","y","width","height","viewBox","preserveAspectRatio","d","points","cx","cy","r","rx","ry","x1","y1","x2","y2","fx","fy","transform","gradientTransform","gradientUnits","spreadMethod","offset","color","display","visibility","fill","fill-opacity","fill-rule","opacity","stop-color","stop-opacity","stroke","stroke-width","stroke-opacity","stroke-linecap","stroke-linejoin","stroke-miterlimit","stroke-dasharray","stroke-dashoffset",NULL};
static const char *listed(const char *name,const char *const *list) { for(;*list;list++) if(!strcmp(name,*list)) return *list;return NULL; }
static void fail(Validation *v,int code,const char *reason) { if(!v->failed) pp_media_error(v->error,code,reason);v->failed=true;XML_StopParser(v->parser,XML_FALSE); }
static char *copy(const char *s,size_t n) { char *p=malloc(n+1);if(p) { memcpy(p,s,n);p[n]=0; }return p; }
static void reference(Validation *v,int node,const char *id,size_t length,bool paint) {
    if(!length || length>256 || v->ref_count==MAX_REFERENCES) { fail(v,PP_ERROR_RESOURCE_LIMIT,"SVG reference limit exceeded");return; }
    Reference *r=&v->refs[v->ref_count]; r->id=copy(id,length);if(!r->id) { fail(v,PP_ERROR_MEMORY,"Could not allocate SVG reference");return; }
    r->next=v->nodes[node].first_ref;r->target=-1;r->paint=paint;v->nodes[node].first_ref=v->ref_count++;
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
    const char *url=strstr(value,"url(");
    if(url) {
        if((strcmp(name,"fill") && strcmp(name,"stroke")) || url!=value || value[4]!='#') { fail(v,PP_ERROR_FORMAT,"Unsupported or external SVG paint reference");return; }
        const char *end=strchr(value+5,')');if(!end || end[1]) { fail(v,PP_ERROR_FORMAT,"Unsupported SVG paint reference syntax");return; }
        reference(v,node,value+5,(size_t)(end-(value+5)),true);
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
    XML_ParserFree(v.parser);for(int i=0;i<v.count;i++) free(v.nodes[i].id);for(int i=0;i<v.ref_count;i++) free(v.refs[i].id);free(v.nodes);free(v.refs);return !v.failed;
}
