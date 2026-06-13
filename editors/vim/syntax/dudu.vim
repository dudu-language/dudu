if exists("b:current_syntax")
  finish
endif

syn keyword duduKeyword def class enum type import from as public private
syn keyword duduControl if elif else while for in return break continue and or not
syn keyword duduBuiltin True False None static_assert
syn keyword duduType bool i8 i16 i32 i64 u8 u16 u32 u64 isize usize f32 f64 void str cstr
syn keyword duduType list dict set tuple Result Option fn const atomic volatile storage shared device
syn match duduDecorator /@[A-Za-z_][A-Za-z0-9_.]*/
syn match duduConstant /\<[A-Z][A-Z0-9_]*\>/
syn match duduNumber /\<[0-9]\+\(\.[0-9]\+\)\?\>/
syn region duduString start=/"/ skip=/\\"/ end=/"/
syn region duduString start=/'/ skip=/\\'/ end=/'/
syn region duduTripleString start=/"""/ end=/"""/
syn match duduComment /#.*/ contains=NONE

hi def link duduKeyword Keyword
hi def link duduControl Conditional
hi def link duduBuiltin Constant
hi def link duduType Type
hi def link duduDecorator PreProc
hi def link duduConstant Constant
hi def link duduNumber Number
hi def link duduString String
hi def link duduTripleString String
hi def link duduComment Comment

let b:current_syntax = "dudu"
