TestData{
    "IPL boot animation :: All NBGs, no illegal accesses",
    0x7744FFFF, // 010,012 CYCA0L/U  CP3 CP3 CP0 CP0 -   -   -   -
    0xFF30FFFF, // 014,016 CYCA1L/U  -   -   PN3 PN0 -   -   -   -
    0x6655FFFF, // 018,01A CYCB0L/U  CP2 CP2 CP1 CP1 -   -   -   -
    0x21FFFFFF, // 01C,01E CYCB1L/U  PN2 PN1 -   -   -   -   -   -
    0x0300, // 00E RAMCTL  A0/A1, B0/B1, no rotparams
    0x8000, // 000 TVMD    lo-res
    0x000F, // 020 BGON    NBG0, NBG1, NBG2, NBG3
    0x1010, // 028 CHCTLA  NBG0 = pal256 scroll 1x1   NBG1 = pal256 scroll 1x1
    0x1022, // 02A CHCTLB  NBG2 = pal256 scroll 1x1   NBG3 = pal256 scroll 1x1
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, true, false, false},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, true, false, false},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Capcom Generation - Dai-5-shuu Kakutouka-tachi :: Art gallery :: Misaligned CP bitmap access on high resolution mode causes CP delay",
    0x44FFFFFF, // 010,012 CYCA0L/U  CP0 CP0 -   -   -   -   -   -
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0xFF44FFFF, // 018,01A CYCB0L/U  -   -   CP0 CP0 -   -   -   -
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x2000, // 00E RAMCTL  A, B, no rotparams
    0x80D2, // 000 TVMD    hi-res
    0x0101, // 020 BGON    NBG0
    0x001E, // 028 CHCTLA  NBG0 = pal256 bitmap
    0x0000, // 02A CHCTLB  -
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction
    0x0000, // 09A SCRCTL  -

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 8, 8},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Capcom Generation - Dai-5-shuu Kakutouka-tachi :: Capcom logo FMV :: Doubled-up VC accesses causes every other read to be skipped",
    0x44444444, // 010,012 CYCA0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0x44444444, // 018,01A CYCB0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xCCFFFFFF, // 01C,01E CYCB1L/U  VC0 VC0 -   -   -   -   -   -
    0x1200, // 00E RAMCTL  A, B0/B1, no rotparams
    0x8000, // 000 TVMD    lo-res
    0x0001, // 020 BGON    NBG0
    0x0042, // 028 CHCTLA  NBG0 = rgb888 bitmap
    0x0000, // 02A CHCTLB  -
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction
    0x0007, // 09A SCRCTL  NBG0 = VC LX LY

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 4,
        },
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    8,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "World Heroes Perfect :: Menus and intro animation :: CP access in illegal slot in T0-T3 in lo-res mode causes skip",
    0x2FF6FEEE, // 010,012 CYCA0L/U  PN2 -   -   CP2 -   SH2 SH2 SH2
    0x0145FEEE, // 014,016 CYCA1L/U  PN0 PN1 CP0 CP1 -   SH2 SH2 SH2
    0x4655FFFF, // 018,01A CYCB0L/U  CP0 CP2 CP1 CP1 -   -   -   -
    0x2F6FFEEF, // 01C,01E CYCB1L/U  PN2 -   CP2 -   -   SH2 SH2 -
    0x1100, // 00E RAMCTL  A0/A1, B, no rotparams
    0x8000, // 000 TVMD    lo-res
    0x0007, // 020 BGON    NBG0, NBG1, NBG2
    0x0101, // 028 CHCTLA  NBG0 = pal16 scroll 2x2  NBG1 = pal16 scroll 2x2
    0x0000, // 02A CHCTLB  NBG2 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction      NBG1 = 1x reduction
    0x0007, // 09A SCRCTL  -

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, true, false, false},
            .charPatAccess = {false, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, true, false, false},
            .charPatAccess = {false, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, false, false, false},
            .charPatAccess = {true, false, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {8, 8, 8, 8},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "X-Men vs. Street Fighter :: Attract mode :: CP access in illegal slot in T4-T7 in lo-res mode causes delay",
    0x5567FEEE, // 010,012 CYCA0L/U  CP1 CP1 CP2 CP3 -   SH2 SH2 SH2
    0x5567FEEE, // 014,016 CYCA1L/U  CP1 CP1 CP2 CP3 -   SH2 SH2 SH2
    0x5567FEEE, // 018,01A CYCB0L/U  CP1 CP1 CP2 CP3 -   SH2 SH2 SH2
    0x112367FE, // 01C,01E CYCB1L/U  PN1 PN1 PN2 PN3 CP2 CP3 -   SH2
    0x1300, // 00E RAMCTL  A0/A1, B0/B1, no rotparams
    0x8001, // 000 TVMD    lo-res
    0x000F, // 020 BGON    NBG0, NBG1, NBG2, NBG3
    0x0100, // 028 CHCTLA  NBG0 = pal16 scroll 1x1  NBG1 = pal16 scroll 2x2
    0x0001, // 02A CHCTLB  NBG2 = pal16 scroll 2x2  NBG3 = pal16 scroll 1x1
    0x0100, // 098 ZMCTL   NBG0 = 1x reduction      NBG1 = 1/2x reduction
    0x0200, // 09A SCRCTL  NBG0 = -                 NBG1 = LX

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {true, true, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {true, true, true, true},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Radiant Silvergun :: Attract mode, first level :: Basic rotation coefficient parameters access",
    0xFEEEEEEE, // 010,012 CYCA0L/U  -   SH2 SH2 SH2 SH2 SH2 SH2 SH2
    0xFEEEEEEE, // 014,016 CYCA1L/U  -   SH2 SH2 SH2 SH2 SH2 SH2 SH2
    0xFFFFFFFF, // 018,01A CYCB0L/U  -   -   -   -   -   -   -   -
    0x00124456, // 01C,01E CYCB1L/U  PN0 PN0 PN1 PN2 CP0 CP0 CP1 CP2
    0x1327, // 00E RAMCTL  A0/A1, B0/B1, rot: A0=charpat A1=coeff B0=patname
    0x8101, // 000 TVMD    lo-res
    0x0017, // 020 BGON    NBG0, NBG1, NBG2, RBG0
    0x0000, // 028 CHCTLA  NBG0 = pal16 scroll 1x1  NBG1 = pal16 scroll 1x1
    0x0000, // 02A CHCTLB  NBG2 = pal16 scroll 1x1
    0x0001, // 098 ZMCTL   NBG0 = 1/2x reduction    NBG1 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 = -                 NBG1 = -

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, false, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, false, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, false, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, true, false, false},
},

TestData{
    "3D Baseball :: Match intro :: Rotparam assignments block CP accesses",
    0x44FEFFFF, // 010,012 CYCA0L/U  CP0 CP0 -   SH2 -   -   -   -
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0x55FEFFFF, // 018,01A CYCB0L/U  CP1 CP1 -   SH2 -   -   -   -
    0x4455FFFF, // 01C,01E CYCB1L/U  CP0 CP0 CP1 CP1 -   -   -   -
    0x031B, // 00E RAMCTL  A0/A1, B0/B1, rot: A0=charpat A1=patname B0=coeff
    0x8012, // 000 TVMD    hi-res
    0x0013, // 020 BGON    NBG0, NBG1, RBG0
    0x1212, // 028 CHCTLA  NBG0 = pal256 bitmap     NBG1 = pal256 bitmap
    0x1000, // 02A CHCTLB  -
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction      NBG1 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 = -                 NBG1 = -

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, false, false, true},  // can't access A0; assigned to rotparams
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, false, false, true},  // can't access B0; assigned to rotparams
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, true, false},
},

TestData{
    "Doukyuusei - if :: Title screen :: No CP delay on hi-res bitmap when each NBG accesses different banks in different phases",
    0x44EEFFFF, // 010,012 CYCA0L/U  CP0 CP0 SH2 SH2 -   -   -   -
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0xEE55FFFF, // 018,01A CYCB0L/U  SH2 SH2 CP1 CP1 -   -   -   -
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x0000, // 00E RAMCTL  A, B, no rotparams
    0x8002, // 000 TVMD    hi-res
    0x0003, // 020 BGON    NBG0, NBG1
    0x1A1A, // 028 CHCTLA  NBG0 = pal256 bitmap       NBG1 = pal256 bitmap
    0x0000, // 02A CHCTLB
    0x0000, // 098 ZMCTL
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, false, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Duke Nukem 3D :: Netlink pages :: Hi-res bitmap modes, legal accesses",
    0x44FE44FE, // 010,012 CYCA0L/U  CP0 CP0 -   SH2 CP0 CP0 -   SH2
    0x44FE44FE, // 014,016 CYCA1L/U  CP0 CP0 -   SH2 CP0 CP0 -   SH2
    0x44FE44FE, // 018,01A CYCB0L/U  CP0 CP0 -   SH2 CP0 CP0 -   SH2
    0x44FE44FE, // 01C,01E CYCB1L/U  CP0 CP0 -   SH2 CP0 CP0 -   SH2
    0x0300, // 00E RAMCTL  A0/A1, B0/B1, no rotparams
    0x80D2, // 000 TVMD    hi-res
    0x0101, // 020 BGON    NBG0
    0x001E, // 028 CHCTLA  NBG0 = pal256 bitmap
    0x0000, // 02A CHCTLB
    0x0000, // 098 ZMCTL
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Baroque Report :: Main menu :: More CP accesses than needed (lo-res, palette 256 bitmap)",
    0x44444444, // 010,012 CYCA0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0x44444444, // 018,01A CYCB0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x1000, // 00E RAMCTL  A, B, no rotparams
    0x8020, // 000 TVMD    lo-res
    0x0001, // 020 BGON    NBG0
    0x0016, // 028 CHCTLA  NBG0 = pal256 bitmap
    0x0000, // 02A CHCTLB
    0x0000, // 098 ZMCTL
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Baroque Report :: Art screens :: More CP accesses than needed (hi-res, palette 256 bitmap)",
    0x44444444, // 010,012 CYCA0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0x44444444, // 018,01A CYCB0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x1000, // 00E RAMCTL  A, B, no rotparams
    0x80E2, // 000 TVMD    hi-res
    0x0001, // 020 BGON    NBG0
    0x0016, // 028 CHCTLA  NBG0 = pal256 bitmap
    0x0000, // 02A CHCTLB
    0x0000, // 098 ZMCTL
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Sonic Jam :: Title screen :: Only T0-T3 matters; extra PN accesses in T4-T7 ignored",
    0x0E440E44, // 010,012 CYCA0L/U  PN0 SH2 CP0 CP0 PN0 SH2 CP0 CP0
    0x0E440E44, // 014,016 CYCA1L/U  PN0 SH2 CP0 CP0 PN0 SH2 CP0 CP0
    0x0E440E44, // 018,01A CYCB0L/U  PN0 SH2 CP0 CP0 PN0 SH2 CP0 CP0
    0x0E440E44, // 01C,01E CYCB1L/U  PN0 SH2 CP0 CP0 PN0 SH2 CP0 CP0
    0x2000, // 00E RAMCTL  A, B, no rotparams
    0x80C2, // 000 TVMD    hi-res
    0x0101, // 020 BGON    NBG0
    0x0011, // 028 CHCTLA  NBG0 = pal256 scroll 2x2
    0x0000, // 02A CHCTLB
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, true, true, true},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Sonic Jam :: Sonic World :: Simple NBG1+NBG2+RBG0 rotparams accesses",
    0xFFFFFFFF, // 010,012 CYCA0L/U  -   -   -   -   -   -   -   -
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0x1526FFFF, // 018,01A CYCB0L/U  PN1 CP1 PN2 CP2 -   -   -   -
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x134B, // 00E RAMCTL  A0/A1, B0/B1, rot: A0=charpat A1=patname B1=coeff
    0x8000, // 000 TVMD    lo-res
    0x1016, // 020 BGON    NBG1, NBG2, RBG0
    0x0011, // 028 CHCTLA                             NBG1 = pal16 scroll 1x1
    0x1100, // 02A CHCTLB  NBG2 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL                              NBG1 = 1x reduction
    0x3200, // 09A SCRCTL                             LX

    // Expected
    {{
        // NBG0
        {},
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {false, false, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {false, false, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, true},
},

TestData{
    "Sonic Jam :: Art gallery :: Rotparams accesses only block CP accesses if RBG is enabled",
    0x44440000, // 010,012 CYCA0L/U  CP0 CP0 CP0 CP0 PN0 PN0 PN0 PN0
    0x44440000, // 014,016 CYCA1L/U  CP0 CP0 CP0 CP0 PN0 PN0 PN0 PN0
    0x44440000, // 018,01A CYCB0L/U  CP0 CP0 CP0 CP0 PN0 PN0 PN0 PN0
    0x44440000, // 01C,01E CYCB1L/U  CP0 CP0 CP0 CP0 PN0 PN0 PN0 PN0
    0x034B, // 00E RAMCTL  A0/A1, B0/B1, rot: A0=charpat A1=patname B1=coeff
    0x80C2, // 000 TVMD    hi-res
    0x0101, // 020 BGON    NBG0
    0x001F, // 028 CHCTLA  NBG0 = pal256 bitmap
    0x1100, // 02A CHCTLB
    0x0000, // 098 ZMCTL
    0x3200, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, true},
},

TestData{
    "Steam-Heart's :: Title screen :: More CP accesses than needed (hi-res, RGB bitmap)",
    0x44444444, // 010,012 CYCA0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0x44444444, // 018,01A CYCB0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x1000, // 00E RAMCTL  A, B, no rotparams
    0x8012, // 000 TVMD    hi-res
    0x1F01, // 020 BGON    NBG0
    0x003A, // 028 CHCTLA  NBG0 = rgb555 bitmap
    0x0000, // 02A CHCTLB
    0x0000, // 098 ZMCTL
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Mr. Bones :: Level 1 (Grave Escape) :: Bitmap doesn't need PN access; unused CP/PN accesses have no side-effects",
    0x446655EE, // 010,012 CYCA0L/U  CP0 CP0 CP2 CP2 CP1 CP1 SH2 SH2
    0x446655EE, // 014,016 CYCA1L/U  CP0 CP0 CP2 CP2 CP1 CP1 SH2 SH2
    0x126655FF, // 018,01A CYCB0L/U  PN1 PN2 CP2 CP2 CP1 CP1 -   -
    0x126655FF, // 01C,01E CYCB1L/U  PN1 PN2 CP2 CP2 CP1 CP1 -   -
    0x2300, // 00E RAMCTL  A0/A1, B0/B1, no rotparams
    0x8100, // 000 TVMD    lo-res
    0x0001, // 020 BGON    NBG0
    0x001A, // 028 CHCTLA  NBG0 = pal256 bitmap
    0x0002, // 02A CHCTLB
    0x0000, // 098 ZMCTL
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Jung Rhythm :: Title screen :: Dual bitmap NBGs (RGB + palette), legal accesses, unused rotparams",
    0x55FEEEEE, // 010,012 CYCA0L/U  CP1 CP1 -   SH2 SH2 SH2 SH2 SH2
    0xFFFEEEEE, // 014,016 CYCA1L/U  -   -   -   SH2 SH2 SH2 SH2 SH2
    0x4444FEEE, // 018,01A CYCB0L/U  CP0 CP0 CP0 CP0 -   SH2 SH2 SH2
    0x4444FEEE, // 01C,01E CYCB1L/U  CP0 CP0 CP0 CP0 -   SH2 SH2 SH2
    0x1327, // 00E RAMCTL  A0/A1, B0/B1, rot: A0=charpat A1=coeff B0=patname
    0x8100, // 000 TVMD    lo-res
    0x0003, // 020 BGON    NBG0, NBG1
    0x1232, // 028 CHCTLA  NBG0 = rgb555 bitmap       NBG1 = pal256 bitmap
    0x1000, // 02A CHCTLB
    0x0000, // 098 ZMCTL
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, false, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, true, false, false},
},

TestData{
    "Road & Track Presents - The Need for Speed :: Main menu :: Dual bitmap NBGs (RGB + RGB), legal accesses, unused rotparams",
    0x44EEEEEE, // 010,012 CYCA0L/U  CP0 CP0 SH2 SH2 SH2 SH2 SH2 SH2
    0x55EEEEEE, // 014,016 CYCA1L/U  CP1 CP1 SH2 SH2 SH2 SH2 SH2 SH2
    0xEEEEEEEE, // 018,01A CYCB0L/U  SH2 SH2 SH2 SH2 SH2 SH2 SH2 SH2
    0xEEEEEEEE, // 01C,01E CYCB1L/U  SH2 SH2 SH2 SH2 SH2 SH2 SH2 SH2
    0x11F0, // 00E RAMCTL  A0/A1, B, rot: B=charpat
    0x8000, // 000 TVMD    lo-res
    0x0013, // 020 BGON    NBG0, NBG1, RBG0
    0x1212, // 028 CHCTLA  NBG0 = pal256 bitmap       NBG1 = pal256 bitmap
    0x1200, // 02A CHCTLB
    0x0000, // 098 ZMCTL
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "The Legend of Oasis :: In-game HUD :: Mixed bitmap + scroll NBGs, legal accesses",
    0x1244EEEE, // 010,012 CYCA0L/U  PN1 PN2 CP0 CP0 SH2 SH2 SH2 SH2
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0x566EEEEE, // 018,01A CYCB0L/U  CP1 CP2 CP2 SH2 SH2 SH2 SH2 SH2
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x0000, // 00E RAMCTL  A, B, no rotparams
    0x8000, // 000 TVMD    lo-res
    0x0007, // 020 BGON    NBG0, NBG1, NBG2
    0x0012, // 028 CHCTLA  NBG0 = pal256 bitmap       NBG1 = pal16 scroll 1x1
    0x0002, // 02A CHCTLB  NBG2 = pal256 scroll 1x1
    0x0000, // 098 ZMCTL                              NBG1 = 1x reduction
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, true, false, false},
            .charPatAccess = {false, false, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, true, false, false},
            .charPatAccess = {false, false, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Street Fighter Zero 3 :: Capcom logo FMV :: Multiple VC accesses assigned to the same NBG cause VC reads to be discarded",
    0x44444444, // 010,012 CYCA0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0x44444444, // 018,01A CYCB0L/U  CP0 CP0 CP0 CP0 CP0 CP0 CP0 CP0
    0xCCFFFFFF, // 01C,01E CYCB1L/U  VC0 VC0 -   -   -   -   -   -
    0x1200, // 00E RAMCTL  A, B0/B1, no rotparams
    0x8000, // 000 TVMD    lo-res
    0x0007, // 020 BGON    NBG0, NBG1, NBG2
    0x0042, // 028 CHCTLA  NBG0 = rgb888 bitmap       NBG1 = pal16 scroll 1x1
    0x0010, // 02A CHCTLB  NBG2 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL                              NBG1 = 1x reduction
    0x0007, // 09A SCRCTL  NBG0 = VC LX LY

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 4,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    8,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "DoDonPachi :: Title screen (background) :: PN and CP access both on T0",
    0x44EEEEEE, // 010,012 CYCA0L/U  CP0 CP0 SH2 SH2 SH2 SH2 SH2 SH2
    0xEEEEEEEE, // 014,016 CYCA1L/U  SH2 SH2 SH2 SH2 SH2 SH2 SH2 SH2
    0x2FEEEEEE, // 018,01A CYCB0L/U  PN2 -   SH2 SH2 SH2 SH2 SH2 SH2
    0x6FEEEEEE, // 01C,01E CYCB1L/U  CP2 -   SH2 SH2 SH2 SH2 SH2 SH2
    0x1227, // 00E RAMCTL  A, B0/B1, rot: A=charpat B0=patname
    0x8100, // 000 TVMD    lo-res
    0x0005, // 020 BGON    NBG0, NBG2
    0x1113, // 028 CHCTLA  NBG0 = pal256 bitmap
    0x1000, // 02A CHCTLB  NBG2 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {true, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {false, false, false, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Gouketsuji Ichizoku 3 - Groove on Fight :: Options screen (scrolling background) :: More PN accesses than needed; PN0/PN1 trigger no CP delays",
    0x0000FFFF, // 010,012 CYCA0L/U  PN0 PN0 PN0 PN0 -   -   -   -
    0x4444FFFF, // 014,016 CYCA1L/U  CP0 CP0 CP0 CP0 -   -   -   -
    0x5555FFFF, // 018,01A CYCB0L/U  CP1 CP1 CP1 CP1 -   -   -   -
    0x5555FFFF, // 01C,01E CYCB1L/U  CP1 CP1 CP1 CP1 -   -   -   -
    0x1300, // 00E RAMCTL  A0/A1, B0/B1, no rotparams
    0x8000, // 000 TVMD    lo-res
    0x0403, // 020 BGON    NBG0, NBG1
    0x0200, // 028 CHCTLA  NBG0 = pal16 scroll 1x1    NBG1 = pal16 bitmap
    0x0000, // 02A CHCTLB
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, false, false, false},
            .charPatAccess = {false, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = true,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, false, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Cyberbots - Fullmetal Madness :: Airport arena, versus mode :: All scroll NBGs, lo-res, legal CP accesses on T0-T3 and T4-T7",
    0x4567FFFF, // 010,012 CYCA0L/U  CP0 CP1 CP2 CP3 -   -   -   -
    0x4567FFFF, // 014,016 CYCA1L/U  CP0 CP1 CP2 CP3 -   -   -   -
    0x01234567, // 018,01A CYCB0L/U  PN0 PN1 PN2 PN3 CP0 CP1 CP2 CP3
    0x4567FFFF, // 01C,01E CYCB1L/U  CP0 CP1 CP2 CP3 -   -   -   -
    0x1000, // 00E RAMCTL  A, B, no rotparams
    0x8001, // 000 TVMD    lo-res
    0x000F, // 020 BGON    NBG0, NBG1, NBG2, NBG3
    0x0000, // 028 CHCTLA  NBG0 = pal16 scroll 1x1    NBG1 = pal16 scroll 1x1
    0x0011, // 02A CHCTLB  NBG2 = pal16 scroll 2x2    NBG3 = pal16 scroll 2x2
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0000, // 09A SCRCTL

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, true},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, true},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, true},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, true},
            .charPatAccess = {true, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Dark Savior :: Title screen :: Hi-res, more scroll CP accesses than needed, illegal accesses have no effect",
    0x5555FFFF, // 010,012 CYCA0L/U  CP1 CP1 CP1 CP1 -   -   -   -
    0x5555FFFF, // 014,016 CYCA1L/U  CP1 CP1 CP1 CP1 -   -   -   -
    0x4444FFFF, // 018,01A CYCB0L/U  CP0 CP0 CP0 CP0 -   -   -   -
    0x0414FFFF, // 01C,01E CYCB1L/U  PN0 CP0 PN1 CP0 -   -   -   -
    0x1300, // 00E RAMCTL  A0/A1, B0/B1, no rotparams
    0x81D3, // 000 TVMD    hi-res
    0x0003, // 020 BGON    NBG0, NBG1
    0x1010, // 028 CHCTLA  NBG0 = pal256 scroll 1x1   NBG1 = pal256 scroll 1x1
    0x1000, // 02A CHCTLB
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 =                     NBG1 =

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {true, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "BattleSport :: Loading screen :: More PN accesses than needed, only first PN matters for CP delays, two scroll NBGs",
    0xFEEEEEEE, // 010,012 CYCA0L/U  -   SH2 SH2 SH2 SH2 SH2 SH2 SH2
    0xFEEEEEEE, // 014,016 CYCA1L/U  -   SH2 SH2 SH2 SH2 SH2 SH2 SH2
    0xFFFFFFFF, // 018,01A CYCB0L/U  -   -   -   -   -   -   -   -
    0x04141155, // 01C,01E CYCB1L/U  PN0 CP0 PN1 CP0 PN1 PN1 CP1 CP1
    0x1327, // 00E RAMCTL  A0/A1, B0/B1, rot: A0=charpat A1=coeff B0=patname
    0x8110, // 000 TVMD    lo-res
    0x0003, // 020 BGON    NBG0, NBG1
    0x1010, // 028 CHCTLA  NBG0 = pal256 scroll 1x1   NBG1 = pal256 scroll 1x1
    0x1000, // 02A CHCTLB
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 =                     NBG1 =

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, false, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, false, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {},
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, true, false, false},
},

TestData{
    "Daisuki :: Intro animation :: More PN accesses than needed, only first PN matters for CP delays, four scroll NBGs",
    0x01323377, // 010,012 CYCA0L/U  PN0 PN1 PN3 PN2 PN3 PN3 CP3 CP3
    0x6666FF66, // 014,016 CYCA1L/U  CP2 CP2 CP2 CP2 -   -   CP2 CP2
    0x55FEEEEE, // 018,01A CYCB0L/U  CP1 CP1 -   SH2 SH2 SH2 SH2 SH2
    0x44FEEEEE, // 01C,01E CYCB1L/U  CP0 CP0 -   SH2 SH2 SH2 SH2 SH2
    0x1327, // 00E RAMCTL  A0/A1, B0/B1, rot: A0=charpat A1=coeff B0=patname
    0x8110, // 000 TVMD    lo-res
    0x000F, // 020 BGON    NBG0, NBG1, NBG2, NBG3
    0x1010, // 028 CHCTLA  NBG0 = pal256 scroll 1x1   NBG1 = pal256 scroll 1x1
    0x1022, // 02A CHCTLB  NBG2 = pal256 scroll 1x1   NBG3 = pal256 scroll 1x1
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 =                     NBG1 =

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, false, false, false},
            .charPatAccess = {false, false, false, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, false, false, false},
            .charPatAccess = {false, false, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, false, false, false},
            .charPatAccess = {false, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, false, false, false},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, true, false, false},
},

TestData{
    "Akumajou Dracula X - Gekka no Yasoukyoku :: Save select screen :: Illegal PN/CP access pair causes delay",
    0x556647FF, // 010,012 CYCA0L/U  CP1 CP1 CP2 CP2 CP0 CP3 -   -
    0xFFFFFFFF, // 014,016 CYCA1L/U  -   -   -   -   -   -   -   -
    0x12FF03FF, // 018,01A CYCB0L/U  PN1 PN2 -   -   PN0 PN3 -   -
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x1200, // 00E RAMCTL  A, B0/B1, no rotparams
    0x8010, // 000 TVMD    lo-res
    0x000F, // 020 BGON    NBG0, NBG1, NBG2, NBG3
    0x1000, // 028 CHCTLA  NBG0 = pal16 scroll 1x1    NBG1 = pal256 scroll 1x1
    0x0002, // 02A CHCTLB  NBG2 = pal256 scroll 1x1   NBG3 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 =                     NBG1 =

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {true, true, false, false},
            .charPatDelay = {true, true, true, true},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {true, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {true, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {true, true, false, false},
            .charPatDelay = {true, true, true, true},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Fighters Megamix :: Character select screen :: Many illegal CP accesses in hi-res mode causes delay",
    0x67FEFFFF, // 010,012 CYCA0L/U  CP2 CP3 -   SH2 -   -   -   -
    0x4455FFFF, // 014,016 CYCA1L/U  CP0 CP0 CP1 CP1 -   -   -   -
    0x0123FFFF, // 018,01A CYCB0L/U  PN0 PN1 PN2 PN3 -   -   -   -
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x0327, // 00E RAMCTL  A0/A1, B0/B1, rot: A0=charpat A1=coeff B0=patname
    0x8103, // 000 TVMD    hi-res
    0x000F, // 020 BGON    NBG0, NBG1, NBG2, NBG3
    0x1010, // 028 CHCTLA  NBG0 = pal256 scroll 1x1   NBG1 = pal256 scroll 1x1
    0x1000, // 02A CHCTLB  NBG2 = pal16 scroll 1x1    NBG3 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 =                     NBG1 =

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {false, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {false, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {true, true, true, true},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {true, true, true, true},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, true, false, false},
},

TestData{
    "Virtua Fighter 2 :: Main menu :: Illegal CP access in hi-res mode causes delay",
    0x65EEEEEE, // 010,012 CYCA0L/U  CP2 CP1 SH2 SH2 SH2 SH2 SH2 SH2
    0x44EEEEEE, // 014,016 CYCA1L/U  CP0 CP0 SH2 SH2 SH2 SH2 SH2 SH2
    0x012EEEEE, // 018,01A CYCB0L/U  PN0 PN1 PN2 SH2 SH2 SH2 SH2 SH2
    0xFFFEEEEE, // 01C,01E CYCB1L/U  -   -   -   SH2 SH2 SH2 SH2 SH2
    0x1327, // 00E RAMCTL  A0/A1, B0/B1, rot: A0=charpat A1=coeff B0=patname
    0x81C3, // 000 TVMD    hi-res
    0x0007, // 020 BGON    NBG0, NBG1, NBG2
    0x0010, // 028 CHCTLA  NBG0 = pal256 scroll 1x1   NBG1 = pal16 scroll 1x1
    0x1000, // 02A CHCTLB  NBG2 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 =                     NBG1 =

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {false, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, false},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {true, true, true, true},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {},
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, true, false, false},
},

TestData{
    "Street Fighter - Real Battle on Film :: Cutscenes (subtitles and some graphics elements) :: Illegal CP access in lo-res mode causes delay",
    0x7FFFFFFF, // 010,012 CYCA0L/U  CP3 -   -   -   -   -   -   -
    0x0144FFFF, // 014,016 CYCA1L/U  PN0 PN1 CP0 CP0 -   -   -   -
    0xFF44F535, // 018,01A CYCB0L/U  -   -   CP0 CP0 -   CP1 PN3 CP1
    0xFFFFFFFF, // 01C,01E CYCB1L/U  -   -   -   -   -   -   -   -
    0x1100, // 00E RAMCTL  A0/A1, B, no rotparams
    0x8010, // 000 TVMD    lo-res
    0x000F, // 020 BGON    NBG0, NBG1, NBG2, NBG3
    0x1111, // 028 CHCTLA  NBG0 = pal256 scroll 2x2   NBG1 = pal256 scroll 2x2
    0x0003, // 02A CHCTLB  NBG2 = pal256 scroll 2x2   NBG3 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 =                     NBG1 =

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, true, false, false},
            .charPatAccess = {false, true, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, true, false, false},
            .charPatAccess = {false, false, true, true},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, false},
            .charPatAccess = {false, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, true, true},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {true, true, true, true},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Daytona USA - Championship Circuit Edition :: Main menu :: All NBGs enabled, multiple illegal CP accesses, only NBG3 affected",
    0x7766FFFF, // 010,012 CYCA0L/U  CP3 CP3 CP2 CP2 -   -   -   -
    0x44556FFF, // 014,016 CYCA1L/U  CP0 CP0 CP1 CP1 CP2 -   -   -
    0x7F55FFFF, // 018,01A CYCB0L/U  CP3 -   CP1 CP1 -   -   -   -
    0x012323FF, // 01C,01E CYCB1L/U  PN0 PN1 PN2 PN3 PN2 PN3 -   -
    0x0300, // 00E RAMCTL  A0/A1, B0/B1, no rotparams
    0x80C3, // 000 TVMD    hi-res
    0x000D, // 020 BGON    NBG0, NBG2, NBG3
    0x1010, // 028 CHCTLA  NBG0 = pal256 scroll 1x1
    0x0000, // 02A CHCTLB  NBG2 = pal16 scroll 1x1    NBG3 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction
    0x0000, // 09A SCRCTL  NBG0 =

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, true, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {},
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {true, false, true, false},
            .charPatDelay = {true, true, true, true},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},

TestData{
    "Daytona USA - Championship Circuit Edition :: In-game :: Scroll CP delay with PN and CP assigned to T4",
    0x4400FFFF, // 010,012 CYCA0L/U  CP0 CP0 PN0 PN0 -   -   -   -
    0xFFFF6FFF, // 014,016 CYCA1L/U  -   -   -   -   CP2 -   -   -
    0xF755FFFF, // 018,01A CYCB0L/U  -   CP3 CP1 CP1 -   -   -   -
    0x0F1F23FF, // 01C,01E CYCB1L/U  PN0 -   PN1 -   PN2 PN3 -   -
    0x1300, // 00E RAMCTL  A0/A1, B0/B1, no rotparams
    0x8001, // 000 TVMD    lo-res
    0x000F, // 020 BGON    NBG0, NBG1, NBG2, NBG3
    0x1010, // 028 CHCTLA  NBG0 = pal256 scroll 1x1   NBG1 = pal256 scroll 1x1
    0x0000, // 02A CHCTLB  NBG2 = pal16 scroll 1x1    NBG3 = pal16 scroll 1x1
    0x0000, // 098 ZMCTL   NBG0 = 1x reduction        NBG1 = 1x reduction
    0x0400, // 09A SCRCTL  NBG0 =                     NBG1 =

    // Expected
    {{
        // NBG0
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {true, false, false, true},
            .charPatAccess = {true, false, false, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG1
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG2
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, true, false, false},
            .charPatDelay = {true, true, true, true},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
        // NBG3
        {
            .check = true,
            .bitmap = false,
            .patNameAccess = {false, false, false, true},
            .charPatAccess = {false, false, true, false},
            .charPatDelay = {false, false, false, false},
            .vramDataOffset = {0, 0, 0, 0},
            .vcellScrollDelay = false,
            .vcellScrollRepeat = false,
            .vcellScrollOffset = 0,
        },
    }},
    // Vertical cell scroll increment
    0,
    // Rotation coefficient accesses
    {false, false, false, false},
},
