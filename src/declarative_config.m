#include "declarative_config.h"
#include <float.h>
#include <math.h>

_Static_assert(DECLARATIVE_CONFIG_COUNT == 36, "config inventory changed without declarative coverage");
_Static_assert(DECLARATIVE_RULE_FIELD_COUNT == 17, "rule inventory changed without declarative coverage");

enum { VALIDATOR_BOOL, VALIDATOR_FFM, VALIDATOR_DISPLAY_ORDER, VALIDATOR_WINDOW_ORIGIN, VALIDATOR_FOCUS_FALLBACK, VALIDATOR_WINDOW_PLACEMENT, VALIDATOR_INSERT_POINT, VALIDATOR_OPACITY_DURATION, VALIDATOR_NONNEGATIVE, VALIDATOR_EASING, VALIDATOR_SHADOW, VALIDATOR_MENUBAR, VALIDATOR_MENUBAR_STYLE, VALIDATOR_SPACE_FILTER, VALIDATOR_UNIT_FLOAT, VALIDATOR_POSITIVE_UNIT, VALIDATOR_NONZERO_U32, VALIDATOR_INT, VALIDATOR_LAYOUT, VALIDATOR_SPLIT_RATIO, VALIDATOR_SPLIT_TYPE, VALIDATOR_AUTO_BALANCE, VALIDATOR_MOUSE_MOD, VALIDATOR_MOUSE_ACTION, VALIDATOR_DROP_ACTION, VALIDATOR_EXTERNAL_BAR };
enum { SETTER_DEBUG_OUTPUT, SETTER_MFF, SETTER_FFM, SETTER_DISPLAY_ORDER, SETTER_WINDOW_ORIGIN, SETTER_WINDOW_FOCUS_FALLBACK, SETTER_WINDOW_PLACEMENT, SETTER_INSERT_POINT, SETTER_WINDOW_ZOOM_PERSIST, SETTER_OPACITY, SETTER_OPACITY_DURATION, SETTER_ANIMATION_DURATION, SETTER_ANIMATION_EASING, SETTER_SHADOW, SETTER_MENUBAR, SETTER_MENUBAR_STYLE, SETTER_SPACE_FILTER, SETTER_MENUBAR_OPACITY, SETTER_ACTIVE_WINDOW_OPACITY, SETTER_NORMAL_WINDOW_OPACITY, SETTER_INSERT_FEEDBACK_COLOR, SETTER_TOP_PADDING, SETTER_BOTTOM_PADDING, SETTER_LEFT_PADDING, SETTER_RIGHT_PADDING, SETTER_LAYOUT, SETTER_WINDOW_GAP, SETTER_SPLIT_RATIO, SETTER_SPLIT_TYPE, SETTER_AUTO_BALANCE, SETTER_MOUSE_MOD, SETTER_MOUSE_ACTION1, SETTER_MOUSE_ACTION2, SETTER_MOUSE_DROP_ACTION, SETTER_EXTERNAL_BAR, SETTER_SKIP_SPACE_ANIMATION };
enum { SYNTAX_METADATA, SYNTAX_FLAG, SYNTAX_MATCHER, SYNTAX_SELECTOR, SYNTAX_SHARED_FLAG, SYNTAX_EFFECT };

const struct declarative_config_descriptor declarative_config_descriptors[] = {
#define X(id, compat, name, kind, scope, validator, setter) [DECLARATIVE_CONFIG_##id] = { name, DECLARATIVE_VALUE_##kind, DECLARATIVE_SCOPE_##scope, VALIDATOR_##validator, SETTER_##setter },
    CONFIG_SETTING_LIST(X)
#undef X
};
const struct declarative_rule_field_descriptor declarative_rule_field_descriptors[] = {
#define X(id, compat, json, cli, query, syntax) [DECLARATIVE_RULE_FIELD_##id] = { json, cli, query, SYNTAX_##syntax },
    RULE_FIELD_LIST(X)
#undef X
};

static int descriptor_lookup(const char *name, size_t length, bool config, bool cli)
{
    if (!name) return -1;
    if (config) {
        for (int i = 0; i < DECLARATIVE_CONFIG_COUNT; ++i) if (strlen(declarative_config_descriptors[i].name) == length && !memcmp(name, declarative_config_descriptors[i].name, length)) return i;
    } else {
        for (int i = 0; i < DECLARATIVE_RULE_FIELD_COUNT; ++i) {
            const char *candidate = cli ? declarative_rule_field_descriptors[i].cli_name : declarative_rule_field_descriptors[i].json_name;
            if (candidate && strlen(candidate) == length && !memcmp(name, candidate, length)) return i;
        }
    }
    return -1;
}
int declarative_config_id_for_name(const char *name, size_t length) { return descriptor_lookup(name, length, true, false); }
int declarative_rule_field_for_json_name(const char *name, size_t length) { return descriptor_lookup(name, length, false, false); }
int declarative_rule_field_for_cli_name(const char *name, size_t length) { return descriptor_lookup(name, length, false, true); }

static void set_error(struct declarative_error *e, enum declarative_error_stage stage, size_t line, size_t column, const char *json_path, const char *message)
{
    if (!e) return;
    e->stage = stage; e->line = line; e->column = column;
    snprintf(e->json_path, sizeof(e->json_path), "%s", json_path ? json_path : "$");
    snprintf(e->message, sizeof(e->message), "%s", message);
}

static bool set_public_path(struct declarative_error *error, const char *path)
{
    static const char suffix[] = "...<truncated>";
    if (!path) return true;
    size_t length = strlen(path);
    if (length >= PATH_MAX || !utf8_validate_bytes(path, length)) {
        set_error(error, DECLARATIVE_ERROR_PATH, 0, 0, "$", length >= PATH_MAX ? "path exceeds limit" : "path is not valid UTF-8");
        return false;
    }
    if (!error) return true;
    size_t output = 0;
    bool truncated = false;
    for (size_t i = 0; i < length;) {
        unsigned char c = (unsigned char)path[i];
        size_t input_width = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        size_t output_width = (c < 0x20 || c == 0x7f) ? 6 : input_width;
        if (output + output_width + sizeof(suffix) > sizeof(error->path)) { truncated = true; break; }
        if (output_width == 6) output += (size_t)snprintf(error->path + output, sizeof(error->path) - output, "\\u%04x", c);
        else { memcpy(error->path + output, path + i, input_width); output += input_width; }
        i += input_width;
    }
    if (truncated) { memcpy(error->path + output, suffix, sizeof(suffix)); }
    else error->path[output] = '\0';
    return true;
}

static void source_position_bytes(const uint8_t *bytes, size_t length, size_t at, size_t *line, size_t *column)
{
    if (at > length) at = length;
    *line = 1; *column = 1;
    for (size_t i = 0; i < at; ++i) {
        if (bytes[i] == '\r') { ++*line; *column = 1; if (i + 1 < at && bytes[i+1] == '\n') ++i; }
        else if (bytes[i] == '\n') { ++*line; *column = 1; }
        else ++*column;
    }
}

struct structural_parser { uint8_t *b; size_t n, p, depth; struct declarative_error *e; const char *target_path; bool target_found; size_t target_at; };
static void source_position(struct structural_parser *p, size_t at, size_t *line, size_t *column)
{
    source_position_bytes(p->b, p->n, at, line, column);
}
static bool structural_fail(struct structural_parser *p, size_t at, const char *path, const char *message)
{
    size_t line, column; source_position(p, at, &line, &column); set_error(p->e, DECLARATIVE_ERROR_JSONC, line, column, path, message); return false;
}
static void skip_ws(struct structural_parser *p) { while (p->p < p->n && (p->b[p->p] == ' ' || p->b[p->p] == '\t' || p->b[p->p] == '\r' || p->b[p->p] == '\n')) ++p->p; }
static bool append_utf8(uint8_t **out, size_t *length, size_t *capacity, uint32_t cp)
{
    uint8_t tmp[4]; size_t count;
    if (cp <= 0x7f) { tmp[0] = cp; count = 1; }
    else if (cp <= 0x7ff) { tmp[0]=0xc0|(cp>>6); tmp[1]=0x80|(cp&63); count=2; }
    else if (cp <= 0xffff) { tmp[0]=0xe0|(cp>>12); tmp[1]=0x80|((cp>>6)&63); tmp[2]=0x80|(cp&63); count=3; }
    else { tmp[0]=0xf0|(cp>>18); tmp[1]=0x80|((cp>>12)&63); tmp[2]=0x80|((cp>>6)&63); tmp[3]=0x80|(cp&63); count=4; }
    if (*length + count > *capacity) { size_t c = *capacity ? *capacity * 2 : 32; while (c < *length + count) c *= 2; void *q = realloc(*out, c); if (!q) return false; *out=q; *capacity=c; }
    memcpy(*out + *length, tmp, count); *length += count; return true;
}
static int hex4(const uint8_t *s) { int v=0; for (int i=0;i<4;++i) { int c=s[i]; int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; if(d<0)return -1; v=(v<<4)|d; } return v; }
static bool parse_string(struct structural_parser *p, uint8_t **decoded, size_t *decoded_length, bool key, const char *path)
{
    size_t start=p->p; if (p->p>=p->n || p->b[p->p++]!='"') return structural_fail(p,start,path,"expected string");
    uint8_t *out=NULL; size_t len=0, cap=0;
    while (p->p<p->n) {
        uint8_t c=p->b[p->p++]; uint32_t cp;
        if (c=='"') { if (key && len>DECLARATIVE_CONFIG_MAX_KEY_BYTES) { free(out); return structural_fail(p,start,path,"decoded key exceeds limit"); } *decoded=out; *decoded_length=len; return true; }
        if (c<0x20) { free(out); return structural_fail(p,p->p-1,path,"control character in string"); }
        if (c!='\\') { if (len+1>cap) { size_t nc=cap?cap*2:32; void *q=realloc(out,nc); if(!q){free(out);set_error(p->e,DECLARATIVE_ERROR_ALLOCATION,0,0,"$","allocation failed");return false;}out=q;cap=nc;} out[len++]=c; continue; }
        if (p->p>=p->n) { free(out); return structural_fail(p,start,path,"unterminated escape"); }
        c=p->b[p->p++];
        if (c=='"'||c=='\\'||c=='/') cp=c;
        else if(c=='b')cp='\b'; else if(c=='f')cp='\f'; else if(c=='n')cp='\n'; else if(c=='r')cp='\r'; else if(c=='t')cp='\t';
        else if(c=='u') {
            if(p->p+4>p->n){free(out);return structural_fail(p,start,path,"invalid unicode escape");} int h=hex4(p->b+p->p); if(h<0){free(out);return structural_fail(p,p->p,path,"invalid unicode escape");} p->p+=4; cp=(uint32_t)h;
            if(cp>=0xd800&&cp<=0xdbff){ if(p->p+6>p->n||p->b[p->p]!='\\'||p->b[p->p+1]!='u'){free(out);return structural_fail(p,start,path,"unpaired surrogate");} int l=hex4(p->b+p->p+2); if(l<0xdc00||l>0xdfff){free(out);return structural_fail(p,start,path,"unpaired surrogate");} p->p+=6; cp=0x10000+((cp-0xd800)<<10)+(l-0xdc00); }
            else if(cp>=0xdc00&&cp<=0xdfff){free(out);return structural_fail(p,start,path,"unpaired surrogate");}
        } else { free(out); return structural_fail(p,p->p-1,path,"invalid string escape"); }
        if(cp==0){free(out);return structural_fail(p,start,path,"decoded U+0000 is not allowed");}
        if(!append_utf8(&out,&len,&cap,cp)){free(out);set_error(p->e,DECLARATIVE_ERROR_ALLOCATION,0,0,path,"allocation failed");return false;}
    }
    free(out); return structural_fail(p,start,path,"unterminated string");
}
static bool parse_structural_value(struct structural_parser *p,const char *path);
static bool schema_known_key(const uint8_t*key,size_t length)
{
    static const char*fixed[]={"version","config","spaces","rules","match","settings","effects","index","uuid","pattern","exclude","mode","top_padding","bottom_padding"};
    for(size_t i=0;i<sizeof(fixed)/sizeof(*fixed);++i)if(strlen(fixed[i])==length&&!memcmp(key,fixed[i],length))return true;
    return declarative_config_id_for_name((const char*)key,length)>=0||declarative_rule_field_for_json_name((const char*)key,length)>=0;
}
static bool parse_object(struct structural_parser *p,const char *path)
{
    if(p->target_path&&!strcmp(path,p->target_path)&&!p->target_found){p->target_found=true;p->target_at=p->p;}if(++p->depth>DECLARATIVE_CONFIG_MAX_DEPTH)return structural_fail(p,p->p,path,"nesting depth exceeds limit");++p->p;skip_ws(p);
    uint8_t*keys[DECLARATIVE_CONFIG_MAX_OBJECT_MEMBERS]={0};size_t lengths[DECLARATIVE_CONFIG_MAX_OBJECT_MEMBERS]={0},count=0;
    if(p->p<p->n&&p->b[p->p]=='}'){++p->p;--p->depth;return true;}
    for(;;){
        if(count==DECLARATIVE_CONFIG_MAX_OBJECT_MEMBERS){structural_fail(p,p->p,path,"object member limit exceeded");goto fail;}
        uint8_t*key=NULL;size_t length=0,key_at=p->p;char key_path[256];snprintf(key_path,sizeof(key_path),"%s.<unknown>",path);if(!parse_string(p,&key,&length,true,key_path))goto fail;
        for(size_t i=0;i<count;++i)if(lengths[i]==length&&!memcmp(keys[i],key,length)){char duplicate[256];snprintf(duplicate,sizeof(duplicate),"%s.<duplicate>",path);free(key);structural_fail(p,key_at,duplicate,"duplicate object key");goto fail;}
        keys[count]=key;lengths[count++]=length;skip_ws(p);if(p->p>=p->n||p->b[p->p++]!=':'){structural_fail(p,p->p,path,"expected colon");goto fail;}skip_ws(p);
        char child[256];if(schema_known_key(key,length))snprintf(child,sizeof(child),"%s.%.*s",path,(int)length,key);else snprintf(child,sizeof(child),"%s.<unknown>",path);if(!parse_structural_value(p,child))goto fail;skip_ws(p);
        if(p->p<p->n&&p->b[p->p]==','){++p->p;skip_ws(p);continue;}if(p->p<p->n&&p->b[p->p]=='}'){++p->p;break;}structural_fail(p,p->p,path,"expected comma or object end");goto fail;
    }
    for(size_t i=0;i<count;++i)free(keys[i]);--p->depth;return true;
fail:for(size_t i=0;i<count;++i)free(keys[i]);--p->depth;return false;
}
static bool parse_array(struct structural_parser*p,const char*path)
{
    if(p->target_path&&!strcmp(path,p->target_path)&&!p->target_found){p->target_found=true;p->target_at=p->p;}if(++p->depth>DECLARATIVE_CONFIG_MAX_DEPTH)return structural_fail(p,p->p,path,"nesting depth exceeds limit");++p->p;skip_ws(p);if(p->p<p->n&&p->b[p->p]==']'){++p->p;--p->depth;return true;}size_t index=0;for(;;){char child[256];snprintf(child,sizeof(child),"%s[%zu]",path,index++);if(p->target_path&&!strcmp(child,p->target_path)&&!p->target_found){p->target_found=true;p->target_at=p->p;}if(!parse_structural_value(p,child)){--p->depth;return false;}skip_ws(p);if(p->p<p->n&&p->b[p->p]==','){++p->p;skip_ws(p);continue;}if(p->p<p->n&&p->b[p->p]==']'){++p->p;--p->depth;return true;}--p->depth;return structural_fail(p,p->p,path,"expected comma or array end");}
}
static bool parse_structural_value(struct structural_parser*p,const char*path)
{
    skip_ws(p);if(p->target_path&&!strcmp(path,p->target_path)&&!p->target_found){p->target_found=true;p->target_at=p->p;}if(p->p>=p->n)return structural_fail(p,p->p,path,"expected value");uint8_t c=p->b[p->p];if(c=='{')return parse_object(p,path);if(c=='[')return parse_array(p,path);if(c=='"'){uint8_t*d=NULL;size_t l=0;bool r=parse_string(p,&d,&l,false,path);free(d);return r;}size_t s=p->p;while(p->p<p->n&&!strchr(" \t\r\n,]}",p->b[p->p]))++p->p;if(s==p->p)return structural_fail(p,s,path,"invalid value");return true;
}

bool declarative_jsonc_normalize(uint8_t *bytes, size_t length, struct declarative_error *error)
{
    if(length && memchr(bytes,0,length)){set_error(error,DECLARATIVE_ERROR_ENCODING,0,0,"$","raw NUL is not allowed");return false;}
    if(!utf8_validate_bytes((char *)bytes,length)){set_error(error,DECLARATIVE_ERROR_ENCODING,0,0,"$","input is not valid UTF-8");return false;}
    if(length>=2&&((bytes[0]==0xff&&bytes[1]==0xfe)||(bytes[0]==0xfe&&bytes[1]==0xff))){set_error(error,DECLARATIVE_ERROR_ENCODING,1,1,"$","unsupported byte order mark");return false;}
    bool leading_bom=length>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf;
    if(leading_bom){bytes[0]=bytes[1]=bytes[2]=' ';}
    for(size_t i=leading_bom?3:0;i+2<length;++i)if(bytes[i]==0xef&&bytes[i+1]==0xbb&&bytes[i+2]==0xbf){set_error(error,DECLARATIVE_ERROR_ENCODING,0,0,"$","byte order mark is only allowed at byte zero");return false;}
    bool string=false,escape=false;
    for(size_t i=0;i<length;++i){if(string){if(escape)escape=false;else if(bytes[i]=='\\')escape=true;else if(bytes[i]=='"')string=false;continue;}if(bytes[i]=='"'){string=true;continue;}if(bytes[i]=='/'&&i+1<length&&bytes[i+1]=='/'){bytes[i]=' '; bytes[i+1]=' '; ++i;while(i+1<length&&bytes[i+1]!='\n'&&bytes[i+1]!='\r')bytes[++i]=' ';}else if(bytes[i]=='/'&&i+1<length&&bytes[i+1]=='*'){size_t start=i;bytes[i]=' '; bytes[i+1]=' '; ++i;bool closed=false;while(i+1<length){if(bytes[i+1]=='*'&&i+2<length&&bytes[i+2]=='/'){bytes[++i]=' ';bytes[++i]=' ';closed=true;break;}++i;if(bytes[i]!='\n'&&bytes[i]!='\r')bytes[i]=' ';}if(!closed){size_t line,column;source_position_bytes(bytes,length,start,&line,&column);set_error(error,DECLARATIVE_ERROR_JSONC,line,column,"$","unterminated block comment");return false;}}}
    struct structural_parser p={.b=bytes,.n=length,.e=error};if(!parse_structural_value(&p,"$"))return false;skip_ws(&p);if(p.p!=length)return structural_fail(&p,p.p,"$","extra input after JSON value");return true;
}

static bool is_number(id object) { return [object isKindOfClass:[NSNumber class]] && CFGetTypeID((CFTypeRef)object) != CFBooleanGetTypeID(); }
bool declarative_number_to_int(NSNumber *n,int min,int max,int *out){if(!is_number(n))return false;double d=[n doubleValue];if(!isfinite(d)||trunc(d)!=d||d<min||d>max)return false;*out=(int)d;return true;}
bool declarative_number_to_uint(NSNumber *n,uint32_t min,uint32_t max,uint32_t*out){if(!is_number(n))return false;double d=[n doubleValue];if(!isfinite(d)||trunc(d)!=d||d<min||d>max)return false;*out=(uint32_t)d;return true;}
bool declarative_number_to_float(NSNumber*n,bool zero,float*out){if(!is_number(n))return false;double d=[n doubleValue];if(!isfinite(d)||(!zero&&d==0.0)||(d!=0.0&&(fabs(d)<FLT_MIN||fabs(d)>FLT_MAX)))return false;float f=(float)d;if(!isfinite(f)||(d!=0.0&&f==0.0))return false;*out=f;return true;}

static void *default_model_calloc(size_t count,size_t size){return calloc(count,size);}static void default_model_free(void*p){free(p);}static const struct declarative_allocator_ops default_allocator_ops={default_model_calloc,default_model_free};static const struct declarative_allocator_ops *allocator_ops=&default_allocator_ops;
#ifdef TESTS
void declarative_config_set_allocator_ops(const struct declarative_allocator_ops*ops){allocator_ops=ops?ops:&default_allocator_ops;}
bool declarative_model_budget_charge(size_t*used,size_t amount){if(!used||*used>DECLARATIVE_CONFIG_MAX_MODEL_BYTES||amount>DECLARATIVE_CONFIG_MAX_MODEL_BYTES-*used)return false;*used+=amount;return true;}
#else
static bool declarative_model_budget_charge(size_t*used,size_t amount){if(!used||*used>DECLARATIVE_CONFIG_MAX_MODEL_BYTES||amount>DECLARATIVE_CONFIG_MAX_MODEL_BYTES-*used)return false;*used+=amount;return true;}
#endif
struct model_builder { struct declarative_config_model *model; size_t bytes; struct declarative_error *error; const uint8_t *source; size_t source_length; };

static bool builder_value_offset(struct model_builder *b, const char *path, size_t *offset)
{
    struct structural_parser finder={(uint8_t *)b->source,b->source_length,0,0,NULL,path,false,0};
    if (!parse_structural_value(&finder,"$") || !finder.target_found) return false;
    *offset=finder.target_at;
    return true;
}

/* Parse a strict-JSON decimal lexeme exactly. Foundation's NSNumber remains the
 * type check, but binary floating point is never used to prove integrality. */
static bool exact_integer_at(struct model_builder *b, size_t at, bool signed_value, int64_t minimum, uint64_t maximum, uint64_t *magnitude, bool *negative)
{
    const uint8_t *s=b->source;size_t n=b->source_length,p=at;
    bool neg=p<n&&s[p]=='-';if(neg)++p;size_t mantissa=p,decimal=SIZE_MAX,digits=0;
    while(p<n&&((s[p]>='0'&&s[p]<='9')||s[p]=='.')){if(s[p]=='.')decimal=p;else ++digits;++p;}
    size_t mantissa_end=p,frac=decimal==SIZE_MAX?0:mantissa_end-decimal-1;
    int64_t exponent=0;bool exp_neg=false;
    if(p<n&&(s[p]=='e'||s[p]=='E')){++p;if(p<n&&(s[p]=='+'||s[p]=='-')){exp_neg=s[p]=='-';++p;}if(p>=n||s[p]<'0'||s[p]>'9')return false;while(p<n&&s[p]>='0'&&s[p]<='9'){int d=s[p++]-'0';if(exponent<(int64_t)b->source_length+64)exponent=exponent*10+d;if(exponent>(int64_t)b->source_length+64)exponent=(int64_t)b->source_length+64;}if(exp_neg)exponent=-exponent;}
    if(!digits||(p<n&&!strchr(" \t\r\n,]}",s[p])))return false;
    bool all_zero=true;for(size_t i=mantissa;i<mantissa_end;++i)if(s[i]>='1'&&s[i]<='9'){all_zero=false;break;}
    if(all_zero){*magnitude=0;*negative=false;return true;}
    if(neg&&(!signed_value||minimum>=0))return false;
    int64_t scale=exponent-(int64_t)frac;size_t remove=0;
    if(scale<0){if(scale<-(int64_t)digits)return false;remove=(size_t)-scale;size_t remaining=remove;for(size_t i=mantissa_end;i>mantissa&&remaining;){uint8_t c=s[--i];if(c=='.')continue;if(c!='0')return false;--remaining;}if(remaining)return false;}
    uint64_t limit=neg?(uint64_t)(-(minimum+1))+1:maximum,value=0;size_t keep=digits-remove,seen=0;
    for(size_t i=mantissa;i<mantissa_end&&seen<keep;++i){if(s[i]=='.')continue;unsigned d=(unsigned)(s[i]-'0');if(value>limit/10||(value==limit/10&&d>limit%10))return false;value=value*10+d;++seen;}
    if(scale>0){if(scale>(int64_t)b->source_length+64)return false;for(int64_t i=0;i<scale;++i){if(value>limit/10)return false;value*=10;}}
    *magnitude=value;*negative=neg;return true;
}
static bool signed_magnitude_to_int64(uint64_t magnitude,bool negative,int64_t*out)
{
    if(!negative){if(magnitude>(uint64_t)INT64_MAX)return false;*out=(int64_t)magnitude;return true;}
    if(magnitude==(uint64_t)INT64_MAX+1){*out=INT64_MIN;return true;}
    if(magnitude>(uint64_t)INT64_MAX)return false;
    *out=-(int64_t)magnitude;return true;
}
static bool builder_number_to_int(struct model_builder*b,id object,const char*path,int minimum,int maximum,int*out)
{
    if(!is_number(object))return false;size_t at;uint64_t mag;bool neg;int64_t value;if(!builder_value_offset(b,path,&at)||!exact_integer_at(b,at,true,minimum,(uint64_t)maximum,&mag,&neg)||!signed_magnitude_to_int64(mag,neg,&value))return false;if(value<minimum||value>maximum)return false;*out=(int)value;return true;
}
static bool builder_number_to_uint(struct model_builder*b,id object,const char*path,uint32_t minimum,uint32_t maximum,uint32_t*out)
{
    if(!is_number(object))return false;size_t at;uint64_t mag;bool neg;if(!builder_value_offset(b,path,&at)||!exact_integer_at(b,at,false,0,maximum,&mag,&neg)||mag<minimum)return false;*out=(uint32_t)mag;return true;
}

static bool builder_error(struct model_builder*b,enum declarative_error_stage stage,const char*path,const char*message){size_t line=0,column=0;if(b->source){const uint8_t*at=NULL;struct structural_parser finder={(uint8_t*)b->source,b->source_length,0,0,NULL,path,false,0};if(parse_structural_value(&finder,"$")&&finder.target_found)at=b->source+finder.target_at;if(!at){const char*p=path;while(*p){if(*p=='.')++p;else if(*p=='['){while(*p&&*p!=']')++p;if(*p)++p;}else{const char*start=p;while(*p&&*p!='.'&&*p!='[')++p;size_t n=(size_t)(p-start);if(n&&*start!='$'&&*start!='<'&&n<140){char needle[144];snprintf(needle,sizeof(needle),"\"%.*s\"",(int)n,start);const uint8_t*found=(const uint8_t*)strstr((const char*)b->source,needle);if(found)at=found;}}}}if(!at)at=b->source;line=1;column=1;for(const uint8_t*q=b->source;q<at;++q){if(*q=='\r'){++line;column=1;if(q+1<at&&q[1]=='\n')++q;}else if(*q=='\n'){++line;column=1;}else ++column;}}set_error(b->error,stage,line,column,path,message);return false;}
static void *model_alloc(struct model_builder*b,size_t n){if(n==0)n=1;size_t charged=b->bytes;if(!declarative_model_budget_charge(&charged,n)){set_error(b->error,DECLARATIVE_ERROR_ALLOCATION,0,0,"$","copied model exceeds limit");return NULL;}void*p=allocator_ops->calloc_fn(1,n);if(!p){set_error(b->error,DECLARATIVE_ERROR_ALLOCATION,0,0,"$","allocation failed");return NULL;}b->bytes=charged;return p;}
static char *model_string(struct model_builder*b,NSString*s,size_t max,bool nonempty){if(![s isKindOfClass:[NSString class]])return NULL;NSUInteger n=[s lengthOfBytesUsingEncoding:NSUTF8StringEncoding];if((nonempty&&n==0)||n>max)return NULL;char*p=model_alloc(b,n+1);if(!p)return NULL;if(![s getCString:p maxLength:n+1 encoding:NSUTF8StringEncoding]){allocator_ops->free_fn(p);return NULL;}return p;}
struct enum_mapping { const char *name; int value; };
static int enum_mapped_value(NSString*s,const struct enum_mapping *values,size_t count){if(![s isKindOfClass:[NSString class]])return -1;for(size_t i=0;i<count;++i)if([s isEqualToString:@(values[i].name)])return values[i].value;return -1;}
static int enum_value(NSString*s,const char **values,size_t count){if(![s isKindOfClass:[NSString class]])return -1;for(size_t i=0;i<count;++i)if([s isEqualToString:@(values[i])])return (int)i;return -1;}
static bool convert_setting(struct model_builder *b,int setting_id,id object,struct declarative_value*out,const char *path)
{
    const char *const bools[]={0};(void)bools;int iv=-1;float f;uint32_t u;out->present=true;
    if(declarative_config_descriptors[setting_id].kind==DECLARATIVE_VALUE_BOOL){if(CFGetTypeID((CFTypeRef)object)!=CFBooleanGetTypeID())return false;out->boolean=[object boolValue];return true;}
    if(declarative_config_descriptors[setting_id].kind==DECLARATIVE_VALUE_INT){if(!builder_number_to_int(b,object,path,INT_MIN,INT_MAX,&iv))return false;out->integer=iv;return true;}
    if(setting_id==DECLARATIVE_CONFIG_INSERT_FEEDBACK_COLOR){if(!builder_number_to_uint(b,object,path,1,UINT32_MAX,&u))return false;out->u32=u;return true;}
    if(declarative_config_descriptors[setting_id].kind==DECLARATIVE_VALUE_FLOAT){if(!declarative_number_to_float(object,setting_id!=DECLARATIVE_CONFIG_ACTIVE_WINDOW_OPACITY&&setting_id!=DECLARATIVE_CONFIG_NORMAL_WINDOW_OPACITY,&f))return false;if(setting_id==DECLARATIVE_CONFIG_OPACITY_DURATION&&(!sa_opacity_duration_is_safe(f)))return false;if(setting_id==DECLARATIVE_CONFIG_ANIMATION_DURATION&&f<0)return false;if((setting_id==DECLARATIVE_CONFIG_MENUBAR_OPACITY)&&(f<0||f>1))return false;if((setting_id==DECLARATIVE_CONFIG_ACTIVE_WINDOW_OPACITY||setting_id==DECLARATIVE_CONFIG_NORMAL_WINDOW_OPACITY)&&(f<=0||f>1))return false;if(setting_id==DECLARATIVE_CONFIG_SPLIT_RATIO&&(f<0.1f||f>0.9f))return false;out->real=f;return true;}
    if(setting_id==DECLARATIVE_CONFIG_EXTERNAL_BAR){
        if(![object isKindOfClass:[NSDictionary class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"expected external_bar object");
        const char*k[]={"mode","top_padding","bottom_padding"};for(NSString *key in object){bool known=false;for(size_t i=0;i<3;++i)if([key isEqualToString:@(k[i])])known=true;if(!known){char p[256];snprintf(p,sizeof(p),"%s.<unknown>",path);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"unknown external_bar field");}}
        for(size_t i=0;i<3;++i)if(!object[@(k[i])]){char p[256];snprintf(p,sizeof(p),"%s.%s",path,k[i]);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"missing required external_bar field");}
        const char*m[]={"off","main","all"};iv=enum_value(object[@"mode"],m,3);if(iv<0){char p[256];snprintf(p,sizeof(p),"%s.mode",path);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"invalid external_bar mode");}out->external_bar.mode=iv;
        char padding_path[256];snprintf(padding_path,sizeof(padding_path),"%s.top_padding",path);if(!builder_number_to_int(b,object[@"top_padding"],padding_path,INT_MIN,INT_MAX,&out->external_bar.top_padding))return builder_error(b,DECLARATIVE_ERROR_SCHEMA,padding_path,"invalid external_bar padding");
        snprintf(padding_path,sizeof(padding_path),"%s.bottom_padding",path);if(!builder_number_to_int(b,object[@"bottom_padding"],padding_path,INT_MIN,INT_MAX,&out->external_bar.bottom_padding))return builder_error(b,DECLARATIVE_ERROR_SCHEMA,padding_path,"invalid external_bar padding");return true;}
    const struct enum_mapping *v=NULL;size_t n=0;
    static const struct enum_mapping ffm[]={{"off",FFM_DISABLED},{"autofocus",FFM_AUTOFOCUS},{"autoraise",FFM_AUTORAISE}}, display[]={{"default",DISPLAY_ARRANGEMENT_ORDER_DEFAULT},{"horizontal",DISPLAY_ARRANGEMENT_ORDER_X},{"vertical",DISPLAY_ARRANGEMENT_ORDER_Y}}, origin[]={{"default",WINDOW_ORIGIN_DEFAULT},{"focused",WINDOW_ORIGIN_FOCUSED},{"cursor",WINDOW_ORIGIN_CURSOR}}, fallback[]={{"off",WINDOW_FOCUS_FALLBACK_OFF},{"recent",WINDOW_FOCUS_FALLBACK_RECENT}}, placement[]={{"first_child",CHILD_FIRST},{"second_child",CHILD_SECOND}}, insert[]={{"focused",INSERT_FOCUSED},{"first",INSERT_FIRST},{"last",INSERT_LAST}}, shadow[]={{"off",PURIFY_ALWAYS},{"float",PURIFY_MANAGED},{"on",PURIFY_DISABLED}}, onoff[]={{"off",MENU_BAR_DISABLED},{"on",MENU_BAR_ENABLED}}, style[]={{"compact",MENU_BAR_STYLE_COMPACT},{"expanded",MENU_BAR_STYLE_EXPANDED}}, filter[]={{"occupied",MENU_BAR_SPACE_FILTER_OCCUPIED},{"all",MENU_BAR_SPACE_FILTER_ALL}}, layout[]={{"bsp",VIEW_BSP},{"stack",VIEW_STACK},{"float",VIEW_FLOAT}}, split[]={{"vertical",SPLIT_Y},{"horizontal",SPLIT_X},{"auto",SPLIT_AUTO}}, balance[]={{"off",SPLIT_NONE},{"on",SPLIT_X|SPLIT_Y},{"x-axis",SPLIT_X},{"y-axis",SPLIT_Y}}, mod[]={{"alt",MOUSE_MOD_ALT},{"shift",MOUSE_MOD_SHIFT},{"cmd",MOUSE_MOD_CMD},{"ctrl",MOUSE_MOD_CTRL},{"fn",MOUSE_MOD_FN}}, action[]={{"move",MOUSE_MODE_MOVE},{"resize",MOUSE_MODE_RESIZE}}, drop[]={{"swap",MOUSE_MODE_SWAP},{"stack",MOUSE_MODE_STACK}};
#define SET(a) do{v=(a);n=sizeof(a)/sizeof(*(a));}while(0)
    switch(setting_id){case DECLARATIVE_CONFIG_FFM:SET(ffm);break;case DECLARATIVE_CONFIG_DISPLAY_ORDER:SET(display);break;case DECLARATIVE_CONFIG_WINDOW_ORIGIN:SET(origin);break;case DECLARATIVE_CONFIG_WINDOW_FOCUS_FALLBACK:SET(fallback);break;case DECLARATIVE_CONFIG_WINDOW_PLACEMENT:SET(placement);break;case DECLARATIVE_CONFIG_WINDOW_INSERT_POINT:SET(insert);break;case DECLARATIVE_CONFIG_SHADOW:SET(shadow);break;case DECLARATIVE_CONFIG_MENUBAR:SET(onoff);break;case DECLARATIVE_CONFIG_MENUBAR_STYLE:SET(style);break;case DECLARATIVE_CONFIG_MENUBAR_SPACE_FILTER:SET(filter);break;case DECLARATIVE_CONFIG_LAYOUT:SET(layout);break;case DECLARATIVE_CONFIG_SPLIT_TYPE:SET(split);break;case DECLARATIVE_CONFIG_AUTO_BALANCE:SET(balance);break;case DECLARATIVE_CONFIG_MOUSE_MOD:SET(mod);break;case DECLARATIVE_CONFIG_MOUSE_ACTION1:case DECLARATIVE_CONFIG_MOUSE_ACTION2:SET(action);break;case DECLARATIVE_CONFIG_MOUSE_DROP_ACTION:SET(drop);break;case DECLARATIVE_CONFIG_ANIMATION_EASING:for(int i=0;i<EASING_TYPE_COUNT;++i)if([object isKindOfClass:[NSString class]]&&[object isEqualToString:@(animation_easing_type_str[i])]){out->enumeration=i;return true;}return false;default:return false;}iv=enum_mapped_value(object,v,n);if(iv<0)return false;out->enumeration=iv;return true;
#undef SET
}
static bool convert_config_object(struct model_builder*b,NSDictionary*d,struct declarative_value*out,bool space,const char*base)
{if(![d isKindOfClass:[NSDictionary class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,base,"expected configuration object");if(space&&[d count]==0)return builder_error(b,DECLARATIVE_ERROR_SCHEMA,base,"settings must not be empty");for(NSString*k in d){if(![k isKindOfClass:[NSString class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,base,"invalid configuration key");const char*s=[k UTF8String];int id=declarative_config_id_for_name(s,strlen(s));if(id<0){char unknown[256];snprintf(unknown,sizeof(unknown),"%s.<unknown>",base);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,unknown,"unknown configuration field");}char path[256];snprintf(path,sizeof(path),"%s.%s",base,declarative_config_descriptors[id].name);if(space&&declarative_config_descriptors[id].scope!=DECLARATIVE_SCOPE_SPACE)return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"field is not valid for a Space");if(!convert_setting(b,id,d[k],&out[id],path))return b->error&&b->error->stage!=DECLARATIVE_ERROR_NONE?false:builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"invalid configuration value");}return true;}
static bool reserved_scratchpad(NSString*s){static const char*v[]={"float","sticky","shadow","split","zoom-parent","zoom-fullscreen","windowed-fullscreen","native-fullscreen","expose","pip","recover"};for(size_t i=0;i<sizeof(v)/sizeof(*v);++i)if([s isEqualToString:@(v[i])])return true;return false;}
static bool convert_rules(struct model_builder*b,id object)
{
    if(![object isKindOfClass:[NSArray class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,"$.rules","expected rules array");NSArray*a=object;if([a count]>DECLARATIVE_CONFIG_MAX_RULES)return builder_error(b,DECLARATIVE_ERROR_SCHEMA,"$.rules","rule limit exceeded");b->model->rule_count=[a count];if(!b->model->rule_count)return true;b->model->rules=model_alloc(b,sizeof(*b->model->rules)*b->model->rule_count);if(!b->model->rules)return false;
    NSMutableSet*labels=[NSMutableSet set];NSString*names[]={@"app",@"title",@"role",@"subrole"};
    for(size_t i=0;i<b->model->rule_count;++i){char base[80];snprintf(base,sizeof(base),"$.rules[%zu]",i);id item=a[i];if(![item isKindOfClass:[NSDictionary class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,base,"expected rule object");NSDictionary*d=item;for(NSString*k in d)if(!([k isEqualToString:@"label"]||[k isEqualToString:@"one_shot"]||[k isEqualToString:@"match"]||[k isEqualToString:@"effects"])){char p[96];snprintf(p,sizeof(p),"%s.<unknown>",base);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"unknown rule field");}struct declarative_rule_model*r=&b->model->rules[i];char path[160];
        if(d[@"label"]){snprintf(path,sizeof(path),"%s.label",base);r->label=model_string(b,d[@"label"],DECLARATIVE_CONFIG_MAX_STRING_BYTES,true);if(!r->label)return b->error&&b->error->stage==DECLARATIVE_ERROR_ALLOCATION?false:builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"invalid rule label");if([labels containsObject:d[@"label"]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"rule labels must be unique");[labels addObject:d[@"label"]];}
        if(d[@"one_shot"]){snprintf(path,sizeof(path),"%s.one_shot",base);if(CFGetTypeID((CFTypeRef)d[@"one_shot"])!=CFBooleanGetTypeID())return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"one_shot must be boolean");r->one_shot=[d[@"one_shot"] boolValue];}
        snprintf(path,sizeof(path),"%s.match",base);id match=d[@"match"];if(!match)return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"missing required match");if(![match isKindOfClass:[NSDictionary class]]||![match count])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"match must be a nonempty object");NSDictionary*m=match;
        for(NSString*k in m){int mi=-1;for(int j=0;j<4;++j)if([k isEqualToString:names[j]])mi=j;if(mi<0){char p[160];snprintf(p,sizeof(p),"%s.match.<unknown>",base);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"unknown matcher");}snprintf(path,sizeof(path),"%s.match.%s",base,[k UTF8String]);id matcher=m[k];if(![matcher isKindOfClass:[NSDictionary class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"matcher must be an object");NSDictionary*md=matcher;for(NSString*mk in md)if(!([mk isEqualToString:@"pattern"]||[mk isEqualToString:@"exclude"])){char p[192];snprintf(p,sizeof(p),"%s.<unknown>",path);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"unknown matcher field");}char pattern_path[192];snprintf(pattern_path,sizeof(pattern_path),"%s.pattern",path);if(!md[@"pattern"])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,pattern_path,"missing required pattern");r->matchers[mi].pattern=model_string(b,md[@"pattern"],DECLARATIVE_CONFIG_MAX_PATTERN_BYTES,true);if(!r->matchers[mi].pattern)return b->error&&b->error->stage==DECLARATIVE_ERROR_ALLOCATION?false:builder_error(b,DECLARATIVE_ERROR_SCHEMA,pattern_path,"invalid matcher pattern");if(md[@"exclude"]){char p[192];snprintf(p,sizeof(p),"%s.exclude",path);if(CFGetTypeID((CFTypeRef)md[@"exclude"])!=CFBooleanGetTypeID())return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"exclude must be boolean");r->matchers[mi].exclude=[md[@"exclude"] boolValue];}regex_t re;if(regcomp(&re,r->matchers[mi].pattern,REG_EXTENDED))return builder_error(b,DECLARATIVE_ERROR_REGEX,pattern_path,"invalid regular expression");regfree(&re);r->matcher_present[mi]=true;}
        id effects=d[@"effects"]?:@{};snprintf(path,sizeof(path),"%s.effects",base);if(![effects isKindOfClass:[NSDictionary class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"effects must be an object");NSDictionary*e=effects;
        for(NSString*k in e){int fid=declarative_rule_field_for_json_name([k UTF8String],[k lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);if(fid<DECLARATIVE_RULE_FIELD_DISPLAY){char p[160];snprintf(p,sizeof(p),"%s.effects.<unknown>",base);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"unknown effect");}id o=e[k];snprintf(path,sizeof(path),"%s.effects.%s",base,declarative_rule_field_descriptors[fid].json_name);
            if(fid==DECLARATIVE_RULE_FIELD_DISPLAY||fid==DECLARATIVE_RULE_FIELD_SPACE){if(![o isKindOfClass:[NSDictionary class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"target must be an object");for(NSString*tk in o)if(![tk isEqualToString:@"index"]){char p[192];snprintf(p,sizeof(p),"%s.<unknown>",path);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"unknown target field");}char p[192];snprintf(p,sizeof(p),"%s.index",path);if(!o[@"index"])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"missing required target index");int x;if(!builder_number_to_int(b,o[@"index"],p,1,INT_MAX,&x))return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"invalid target index");if(fid==DECLARATIVE_RULE_FIELD_DISPLAY){r->display_present=true;r->display_index=x;}else{r->space_present=true;r->space_index=x;}}
            else if(fid==DECLARATIVE_RULE_FIELD_FOLLOW){if(CFGetTypeID((CFTypeRef)o)!=CFBooleanGetTypeID())return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"follow must be boolean");r->follow_present=true;r->follow=[o boolValue];}
            else if(fid==DECLARATIVE_RULE_FIELD_GRID){if(![o isKindOfClass:[NSArray class]]||[o count]!=6)return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"grid must contain six elements");for(int j=0;j<6;++j){uint32_t x;char p[192];snprintf(p,sizeof(p),"%s[%d]",path,j);if(!builder_number_to_uint(b,o[j],p,j<2?1:0,UINT_MAX,&x))return builder_error(b,DECLARATIVE_ERROR_SCHEMA,p,"invalid grid element");r->grid[j]=x;}r->grid_present=true;}
            else if(fid==DECLARATIVE_RULE_FIELD_OPACITY){if(!declarative_number_to_float(o,true,&r->opacity)||r->opacity<0||r->opacity>1)return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"invalid opacity");r->opacity_present=true;}
            else if(fid==DECLARATIVE_RULE_FIELD_SUB_LAYER){static const struct enum_mapping v[]={{"below",LAYER_BELOW},{"normal",LAYER_NORMAL},{"above",LAYER_ABOVE},{"auto",LAYER_AUTO}};r->layer=enum_mapped_value(o,v,4);if(r->layer<0)return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"invalid sub_layer");r->layer_present=true;}
            else if(fid==DECLARATIVE_RULE_FIELD_SCRATCHPAD){r->scratchpad=model_string(b,o,DECLARATIVE_CONFIG_MAX_STRING_BYTES,true);if(!r->scratchpad)return b->error&&b->error->stage==DECLARATIVE_ERROR_ALLOCATION?false:builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"invalid scratchpad");if(reserved_scratchpad(o))return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"reserved scratchpad");}
            else {if(CFGetTypeID((CFTypeRef)o)!=CFBooleanGetTypeID())return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"effect must be boolean");bool x=[o boolValue];if(fid==DECLARATIVE_RULE_FIELD_MANAGE){r->manage_present=true;r->manage=x;}else if(fid==DECLARATIVE_RULE_FIELD_STICKY){r->sticky_present=true;r->sticky=x;}else if(fid==DECLARATIVE_RULE_FIELD_MFF){r->mff_present=true;r->mff=x;}else if(fid==DECLARATIVE_RULE_FIELD_FULLSCREEN){r->fullscreen_present=true;r->fullscreen=x;}else return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"invalid effect");}}
        if(r->follow_present&&!r->display_present&&!r->space_present){snprintf(path,sizeof(path),"%s.effects.follow",base);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"follow requires display or space");}if(r->scratchpad&&r->manage_present){snprintf(path,sizeof(path),"%s.effects",base);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,path,"scratchpad conflicts with manage");}
    }return true;
}
static bool convert_root(id root,struct model_builder*b)
{
    if(![root isKindOfClass:[NSDictionary class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,"$","expected top-level object");NSDictionary*d=root;for(NSString*k in d)if(!([k isEqualToString:@"version"]||[k isEqualToString:@"config"]||[k isEqualToString:@"spaces"]||[k isEqualToString:@"rules"]))return builder_error(b,DECLARATIVE_ERROR_SCHEMA,"$.<unknown>","unknown top-level field");int version;if(!d[@"version"])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,"$.version","missing required version");if(!builder_number_to_int(b,d[@"version"],"$.version",1,1,&version))return builder_error(b,DECLARATIVE_ERROR_SCHEMA,"$.version","version must equal 1");if(d[@"config"]&&!convert_config_object(b,d[@"config"],b->model->config,false,"$.config"))return false;
    if(d[@"spaces"]){id spaces=d[@"spaces"];if(![spaces isKindOfClass:[NSArray class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,"$.spaces","expected spaces array");NSArray*a=spaces;if([a count]>DECLARATIVE_CONFIG_MAX_SPACES)return builder_error(b,DECLARATIVE_ERROR_SCHEMA,"$.spaces","Space policy limit exceeded");b->model->space_count=[a count];if(b->model->space_count){b->model->spaces=model_alloc(b,sizeof(*b->model->spaces)*b->model->space_count);if(!b->model->spaces)return false;}for(size_t i=0;i<b->model->space_count;++i){char base[96];snprintf(base,sizeof(base),"$.spaces[%zu]",i);id item=a[i];if(![item isKindOfClass:[NSDictionary class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,base,"expected Space policy object");NSDictionary*s=item;for(NSString *key in s)if(!([key isEqualToString:@"match"]||[key isEqualToString:@"settings"])){char pth[112];snprintf(pth,sizeof(pth),"%s.<unknown>",base);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,pth,"unknown Space policy field");}char match_path[112];snprintf(match_path,sizeof(match_path),"%s.match",base);if(!s[@"match"])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,match_path,"missing required match");if(!s[@"settings"]){char pth[112];snprintf(pth,sizeof(pth),"%s.settings",base);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,pth,"missing required settings");}id match=s[@"match"];if(![match isKindOfClass:[NSDictionary class]])return builder_error(b,DECLARATIVE_ERROR_SCHEMA,match_path,"Space match must be an object");NSDictionary*m=match;for(NSString*key in m)if(!([key isEqualToString:@"index"]||[key isEqualToString:@"uuid"])){char pth[128];snprintf(pth,sizeof(pth),"%s.<unknown>",match_path);return builder_error(b,DECLARATIVE_ERROR_SCHEMA,pth,"unknown Space matcher field");}if([m count]!=1)return builder_error(b,DECLARATIVE_ERROR_SCHEMA,match_path,"Space match requires exactly one selector");struct declarative_space_policy*p=&b->model->spaces[i];if(m[@"index"]){char pth[128];snprintf(pth,sizeof(pth),"%s.index",match_path);if(!builder_number_to_int(b,m[@"index"],pth,1,INT_MAX,&p->index))return builder_error(b,DECLARATIVE_ERROR_SCHEMA,pth,"invalid Space index");p->by_index=true;}else if(m[@"uuid"]){char pth[128];snprintf(pth,sizeof(pth),"%s.uuid",match_path);p->uuid=model_string(b,m[@"uuid"],DECLARATIVE_CONFIG_MAX_STRING_BYTES,true);if(!p->uuid)return b->error&&b->error->stage==DECLARATIVE_ERROR_ALLOCATION?false:builder_error(b,DECLARATIVE_ERROR_SCHEMA,pth,"invalid Space uuid");}else return builder_error(b,DECLARATIVE_ERROR_SCHEMA,match_path,"Space match requires a selector");char settings_path[112];snprintf(settings_path,sizeof(settings_path),"%s.settings",base);if(!convert_config_object(b,s[@"settings"],p->settings,true,settings_path))return false;}}
    if(d[@"rules"]&&!convert_rules(b,d[@"rules"]))return false;return true;
}
void declarative_config_destroy(struct declarative_config_model*m){if(!m)return;void(*release)(void*)=m->allocator_free?m->allocator_free:default_model_free;if(m->spaces)for(size_t i=0;i<m->space_count;++i)release(m->spaces[i].uuid);if(m->rules)for(size_t i=0;i<m->rule_count;++i){struct declarative_rule_model*r=&m->rules[i];release(r->label);for(int j=0;j<4;++j)release(r->matchers[j].pattern);release(r->scratchpad);}release(m->spaces);release(m->rules);release(m);}

bool declarative_config_parse_bytes(const uint8_t*source,size_t length,const char*path,struct declarative_config_model**out,struct declarative_error*error)
{
    if(out)*out=NULL;if(error)memset(error,0,sizeof(*error));if(!set_public_path(error,path))return false;if(!source||length>DECLARATIVE_CONFIG_MAX_FILE_SIZE){set_error(error,DECLARATIVE_ERROR_FILE,0,0,"$","file exceeds size limit");return false;}uint8_t*b=malloc(length+1);if(!b){set_error(error,DECLARATIVE_ERROR_ALLOCATION,0,0,"$","allocation failed");return false;}memcpy(b,source,length);b[length]=0;if(!declarative_jsonc_normalize(b,length,error)){free(b);return false;}
    @autoreleasepool { NSError*ns_error=nil;id root=[NSJSONSerialization JSONObjectWithData:[NSData dataWithBytesNoCopy:b length:length freeWhenDone:NO] options:0 error:&ns_error];if(!root){size_t line=0,column=0;NSNumber*index=ns_error.userInfo[@"NSJSONSerializationErrorIndex"];if(index)source_position_bytes(b,length,MIN((size_t)[index unsignedLongLongValue],length),&line,&column);set_error(error,DECLARATIVE_ERROR_JSON,line,column,"$","invalid JSON syntax");free(b);return false;}struct model_builder builder={0};builder.error=error;builder.source=b;builder.source_length=length;builder.model=allocator_ops->calloc_fn(1,sizeof(*builder.model));if(!builder.model){set_error(error,DECLARATIVE_ERROR_ALLOCATION,0,0,"$","allocation failed");free(b);return false;}builder.model->allocator_free=allocator_ops->free_fn;builder.bytes=sizeof(*builder.model);if(builder.bytes>DECLARATIVE_CONFIG_MAX_MODEL_BYTES||!convert_root(root,&builder)){ if(error&&error->stage==DECLARATIVE_ERROR_NONE)set_error(error,DECLARATIVE_ERROR_SCHEMA,0,0,"$","configuration does not match schema");declarative_config_destroy(builder.model);free(b);return false;}builder.model->allocated_bytes=builder.bytes;if(out)*out=builder.model;else declarative_config_destroy(builder.model); }
    free(b);return true;
}

static int real_open(const char*p,int f){return open(p,f);}static ssize_t real_read(int f,void*b,size_t n){return read(f,b,n);}static int real_fstat(int f,struct stat*s){return fstat(f,s);}static int real_close(int f){return close(f);}static const struct declarative_file_ops default_ops={real_open,real_read,real_fstat,real_close};static const struct declarative_file_ops*file_ops=&default_ops;
#ifdef TESTS
void declarative_config_set_file_ops(const struct declarative_file_ops*ops){file_ops=ops?ops:&default_ops;}
#endif
static bool same_stat(struct stat*a,struct stat*b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&(a->st_mode&S_IFMT)==(b->st_mode&S_IFMT)&&a->st_size==b->st_size&&a->st_mtimespec.tv_sec==b->st_mtimespec.tv_sec&&a->st_mtimespec.tv_nsec==b->st_mtimespec.tv_nsec&&a->st_ctimespec.tv_sec==b->st_ctimespec.tv_sec&&a->st_ctimespec.tv_nsec==b->st_ctimespec.tv_nsec;}
static int descriptor_fstat(int fd, struct stat *st){int result;do result=file_ops->fstat_fn(fd,st);while(result<0&&errno==EINTR);return result;}
bool declarative_config_validate_path(const char *path, struct declarative_error *error)
{
    if (error) memset(error, 0, sizeof(*error));
    if (!path) { set_error(error, DECLARATIVE_ERROR_PATH, 0, 0, "$", "path is required"); return false; }
    return set_public_path(error, path);
}
enum declarative_file_result declarative_config_parse_optional_file(const char*path,struct declarative_config_model**model,struct declarative_error*error)
{
    if(model)*model=NULL;if(error)memset(error,0,sizeof(*error));if(!path||!set_public_path(error,path)){if(!path)set_error(error,DECLARATIVE_ERROR_PATH,0,0,"$","path is required");return DECLARATIVE_FILE_ERROR;}int fd;do fd=file_ops->open_fn(path,O_RDONLY|O_CLOEXEC);while(fd<0&&errno==EINTR);if(fd<0){if(errno==ENOENT)return DECLARATIVE_FILE_ABSENT;set_error(error,DECLARATIVE_ERROR_OPEN,0,0,"$","could not open configuration");return DECLARATIVE_FILE_ERROR;}bool ok=false;struct stat first,last;uint8_t*bytes=NULL;size_t used=0;if(descriptor_fstat(fd,&first)<0||!S_ISREG(first.st_mode)){set_error(error,DECLARATIVE_ERROR_FILE,0,0,"$","configuration is not a regular file");goto done;}if(first.st_size>DECLARATIVE_CONFIG_MAX_FILE_SIZE){set_error(error,DECLARATIVE_ERROR_FILE,0,0,"$","file exceeds size limit");goto done;}bytes=malloc(DECLARATIVE_CONFIG_MAX_FILE_SIZE+1);if(!bytes){set_error(error,DECLARATIVE_ERROR_ALLOCATION,0,0,"$","allocation failed");goto done;}for(;;){ssize_t n=file_ops->read_fn(fd,bytes+used,DECLARATIVE_CONFIG_MAX_FILE_SIZE+1-used);if(n<0&&errno==EINTR)continue;if(n<0){set_error(error,DECLARATIVE_ERROR_READ,0,0,"$","could not read configuration");goto done;}if(n==0)break;used+=(size_t)n;if(used>DECLARATIVE_CONFIG_MAX_FILE_SIZE){set_error(error,DECLARATIVE_ERROR_FILE,0,0,"$","file exceeds size limit");goto done;}}if(descriptor_fstat(fd,&last)<0){set_error(error,DECLARATIVE_ERROR_READ,0,0,"$","could not verify configuration");goto done;}if(!same_stat(&first,&last)||(off_t)used!=first.st_size){set_error(error,DECLARATIVE_ERROR_FILE,0,0,"$","configuration changed while reading");goto done;}ok=declarative_config_parse_bytes(bytes,used,path,model,error);
done:free(bytes);while(file_ops->close_fn(fd)<0&&errno==EINTR){}return ok?DECLARATIVE_FILE_OK:DECLARATIVE_FILE_ERROR;
}
bool declarative_config_parse_file(const char*path,struct declarative_config_model**model,struct declarative_error*error)
{
    enum declarative_file_result result=declarative_config_parse_optional_file(path,model,error);
    if(result==DECLARATIVE_FILE_ABSENT)set_error(error,DECLARATIVE_ERROR_OPEN,0,0,"$","could not open configuration");
    return result==DECLARATIVE_FILE_OK;
}
