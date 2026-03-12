#ifndef F0D81452_7F72_4701_BE58_D289635B3CBC
#define F0D81452_7F72_4701_BE58_D289635B3CBC

#ifndef PRETTY_FUNCTION
    #if defined(__GNUC__) || (defined(__clang__) && !defined(_MSC_VER))
        #define PRETTY_FUNCTION __PRETTY_FUNCTION__
    #elif defined(_MSC_VER)
        #define PRETTY_FUNCTION __FUNCSIG__
    #else
        #define PRETTY_FUNCTION __func__
    #endif
#endif

#endif /* F0D81452_7F72_4701_BE58_D289635B3CBC */
