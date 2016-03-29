/*
 *
 * Intel ACPI Component Architecture
 * ASL Optimizing Compiler version 20140214-64 [Mar 29 2014]
 * Copyright (c) 2000 - 2014 Intel Corporation
 *
 * Compilation of "vmm_acpi_dsdt.dsl" - Fri Apr  1 13:34:26 2016
 *
 */

    /*
     *
     * Based on the example at osdev wiki wiki.osdev.org/AML,
     * and the other example in http://www.acpi.info/DOWNLOADS/ACPI_5_Errata%20A.pdf
     * on page 194
     *
     * Compiled with `iasl -sc input_file.dsl`
    */

    /*
     *       9:  DefinitionBlock (
     *      10:      "vmm_acpi_dsdt.aml", // Output AML Filename : String
     *      11:      "DSDT",              // Signature : String
     *      12:      0x2,                 // DSDT Compliance Revision : ByteConst
     *      13:      "MIKE",              // OEMID : String
     *      14:      "DSDTTBL",           // TABLE ID : String
     *      15:      0x0                  // OEM Revision : DWordConst
     *      16:  ){}
     */
    unsigned char    DSDT_DSDTTBL_Header [] =
    {
        0x44,0x53,0x44,0x54,0x24,0x00,0x00,0x00,    /* 00000000    "DSDT$..." */
        0x02,0xF3,0x4D,0x49,0x4B,0x45,0x00,0x00,    /* 00000008    "..MIKE.." */
        0x44,0x53,0x44,0x54,0x54,0x42,0x4C,0x00,    /* 00000010    "DSDTTBL." */
        0x00,0x00,0x00,0x00,0x49,0x4E,0x54,0x4C,    /* 00000018    "....INTL" */
        0x14,0x02,0x14,0x20,                        /* 00000020    "... " */
    /*

     */
    };
