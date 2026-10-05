#import "macos_internal.h"
#import <CoreText/CoreText.h>
#include <stdlib.h>
#include <string.h>
bool pp_platform_find_font(const char *family,PPBlob *out,PPError *error) {
    if(out)*out=(PPBlob){0};
    if(!family||!out)return pp_mac_error(error,PP_ERROR_ARGUMENT,"Missing font family or output");
    @autoreleasepool {
        NSString *name=[NSString stringWithUTF8String:family];
        if(!name)return pp_mac_error(error,PP_ERROR_ARGUMENT,"Invalid UTF-8 font family");
        NSDictionary *attributes=@{(id)kCTFontFamilyNameAttribute:name};
        CTFontDescriptorRef query=CTFontDescriptorCreateWithAttributes((CFDictionaryRef)attributes);
        NSSet *mandatory=[NSSet setWithObject:(id)kCTFontFamilyNameAttribute];
        CTFontDescriptorRef match=CTFontDescriptorCreateMatchingFontDescriptor(query,(CFSetRef)mandatory);
        CFRelease(query);
        CFURLRef url=match?CTFontDescriptorCopyAttribute(match,kCTFontURLAttribute):NULL;
        NSData *data=url?[NSData dataWithContentsOfURL:(NSURL *)url]:nil;
        if(url)CFRelease(url);if(match)CFRelease(match);
        /* Office keeps some optional fonts private. Read source bytes only;
         * never register a project font globally. Inspection enforces rights. */
        if(!data&&[name caseInsensitiveCompare:@"Aptos"]==NSOrderedSame) {
            NSArray *roots=@[@"/Applications/Microsoft Word.app/Contents/Resources/DFonts",
                @"/Applications/Microsoft PowerPoint.app/Contents/Resources/DFonts",
                [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Fonts"]];
            for(NSString *root in roots) {
                data=[NSData dataWithContentsOfFile:[root stringByAppendingPathComponent:@"Aptos.ttf"]];
                if(data)break;
            }
        }
        if(!data.length)return pp_mac_error(error,PP_ERROR_PLATFORM,"Requested local font family is unavailable");
        out->data=malloc(data.length);
        if(!out->data)return pp_mac_error(error,PP_ERROR_MEMORY,"Cannot copy local font");
        memcpy(out->data,data.bytes,data.length);out->size=data.length;return true;
    }
}
