/**************************************************************************************************
 *  IOWOW library
 *  Copyright (C) 2012-2015 Softmotions Ltd <info@softmotions.com>
 *
 *  This file is part of IOWOW.
 *  IOWOW is free software; you can redistribute it and/or modify it under the terms of
 *  the GNU Lesser General Public License as published by the Free Software Foundation; either
 *  version 2.1 of the License or any later version. IOWOW is distributed in the hope
 *  that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 *  License for more details.
 *  You should have received a copy of the GNU Lesser General Public License along with IOWOW;
 *  if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 *  Boston, MA 02111-1307 USA.
 *************************************************************************************************/

#include "iwcfg.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <CUnit/Basic.h>
#include "iwlog.h"
#include "iwutils.h"

int init_suite(void) {
    int rc = iwlog_init();
    return rc;
}

int clean_suite(void) {
    return 0;
}

void iwlog_test1() {
    uint32_t ec = (0xfffffffdU & 0x3fffffffU);
    uint64_t rc = 0xfafafafaULL;
    rc = iwrc_set_errno(rc, ec); 
    uint32_t ec2 = iwrc_strip_errno(&rc);
    CU_ASSERT_EQUAL(ec, ec2);
    CU_ASSERT_EQUAL(rc, 0xfafafafaULL);
}


void iwlog_test2() {
    IWLOG_DEFAULT_OPTS opts = {0};
    int rv = 0;
    size_t sz;
    char fname[] = "iwlog_test1_XXXXXX";
    int fd = mkstemp(fname);
    CU_ASSERT_TRUE(fd != 1);
    FILE *out = fdopen(fd, "w");
    CU_ASSERT_PTR_NOT_NULL(out);

    fprintf(stderr, "Redirecting log to: %s" IW_LINE_SEP, fname);

    opts.out = out;
    iwlog_set_logfn_opts(&opts);

    iwlog_info2("7fa79c75beac413d83f35ffb6bf571b9");
    iwlog_error("7e94f7214af64513b30ab4df3f62714a%s", "C");
    iwlog_ecode_warn(IW_ERROR_READONLY, "c94645c3b107433497ef295b1c00dcff%d", 12);
    

    errno = ENOENT;
    iwrc ecode = iwrc_set_errno(IW_ERROR_ERRNO, errno);
    rv = iwlog(IWLOG_DEBUG, ecode, NULL, 0, "ERRNO Message");
    CU_ASSERT_EQUAL(rv, 0);
    errno = 0;
    fclose(out);

    out = fopen(fname, "r");
    CU_ASSERT_PTR_NOT_NULL_FATAL(out);

    char buf[1024];
    memset(buf, 0, 1024);
    sz = fread(buf, 1, 1024, out);
    CU_ASSERT_TRUE(sz);
    fprintf(stderr, "%s\n\n" IW_LINE_SEP, buf);

    CU_ASSERT_PTR_NOT_NULL(strstr(buf, "7fa79c75beac413d83f35ffb6bf571b9"));
    CU_ASSERT_PTR_NOT_NULL(strstr(buf, "7e94f7214af64513b30ab4df3f62714aC"));
    CU_ASSERT_PTR_NOT_NULL(strstr(buf, "DEBUG 70001|2|0|Error with expected errno status set. (IW_ERROR_ERRNO)|"));
    CU_ASSERT_PTR_NOT_NULL(strstr(buf, "ERRNO Message"));
    CU_ASSERT_PTR_NOT_NULL(strstr(buf, "ERROR iwlog_test1.c:"));
    CU_ASSERT_PTR_NOT_NULL(strstr(buf, "70004|0|0|Resource is readonly. (IW_ERROR_READONLY)|"));
    CU_ASSERT_PTR_NOT_NULL(strstr(buf, "c94645c3b107433497ef295b1c00dcff12"));
    

    fclose(out);
    unlink(fname);
}

int main() {
    setlocale(LC_ALL, "en_US.UTF-8");
    CU_pSuite pSuite = NULL;

    /* Initialize the CUnit test registry */
    if (CUE_SUCCESS != CU_initialize_registry())
        return CU_get_error();

    /* Add a suite to the registry */
    pSuite = CU_add_suite("iwlog_test1", init_suite, clean_suite);

    if (NULL == pSuite) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    /* Add the tests to the suite */
    if (
        (NULL == CU_add_test(pSuite, "iwlog_test1", iwlog_test1)) ||
        (NULL == CU_add_test(pSuite, "iwlog_test2", iwlog_test2))
       ) {
        CU_cleanup_registry();
        return CU_get_error();
    }

    /* Run all tests using the CUnit Basic interface */
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    int ret = CU_get_error() || CU_get_number_of_failures();
    CU_cleanup_registry();
    return ret;
}
