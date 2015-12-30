/* Copyright 2013 Baruch Even
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "scsicmd.h"
#include "main.h"
#include "sense_dump.h"
#include "scsicmd_utils.h"
#include "parse_extended_inquiry.h"
#include "parse_receive_diagnostics.h"
#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <scsi/sg.h>
#include <inttypes.h>
#include <ctype.h>

static void hex_dump(uint8_t *data, uint16_t len)
{
	uint16_t i;

	if (data == NULL || len == 0)
		return;

	printf("%02x", data[0]);
	for (i = 1; i < len; i++) {
		printf(" %02x", data[i]);
	}
}

static void emit_data_csv(uint8_t *cdb, uint8_t cdb_len, uint8_t *sense, uint8_t sense_len, uint8_t *buf, uint16_t buf_len)
{
	putchar(',');
	hex_dump(cdb, cdb_len);
	putchar(',');
	hex_dump(sense, sense_len);
	putchar(',');
	hex_dump(buf, buf_len);
	putchar('\n');
}

static void do_simple_inquiry(int fd)
{
	unsigned char cdb[32];
	unsigned char buf[512];
	unsigned cdb_len = cdb_inquiry_simple(cdb, sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		printf("Failed to submit command,\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
} 

static void dump_evpd(int fd, uint8_t evpd_page)
{
	unsigned char cdb[32];
	unsigned char buf[512];
	unsigned cdb_len = cdb_inquiry(cdb, true, evpd_page, sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		printf("Failed to submit command,\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
}

static void do_extended_inquiry(int fd)
{
	unsigned char cdb[32];
	unsigned char buf[512];
	unsigned cdb_len = cdb_inquiry(cdb, true, 0, sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		printf("Failed to submit command,\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);

	if (!sense) {
		uint16_t max_page_idx = evpd_page_len(buf) + 4;
		uint16_t i;
		for (i = 4; i < max_page_idx; i++)
			dump_evpd(fd, buf[i]);
	}
}

static void dump_log_sense(int fd, uint8_t page, uint8_t subpage)
{
	unsigned char cdb[32];
	unsigned char buf[16*1024];
	unsigned cdb_len = cdb_log_sense(cdb, page, subpage, sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
}

static void do_log_sense(int fd)
{
	unsigned char cdb[32];
	unsigned char buf[16*1024];
	unsigned cdb_len = cdb_log_sense(cdb, 0, 0, sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);

	if (sense) {
		printf("error while reading log sense list, nothing to show\n");
		return;
	}

	if (buf_len < 4) {
		printf("log sense list must have at least 4 bytes\n");
		return;
	}

	if (buf[0] != 0 || buf[1] != 0) {
		printf("expected to receive log page 0 subpage 0\n");
		return;
	}

	uint16_t num_pages = get_uint16(buf, 2);
	uint16_t i;
	for (i = 0; i < num_pages; i++) {
		dump_log_sense(fd, buf[4 + i], 0);
	}

	cdb_len = cdb_log_sense(cdb, 0, 0xff, sizeof(buf));

	memset(buf, 0, sizeof(buf));

	ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	sense = NULL;
	sense_len = 0;
	buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);

	if (sense) {
		printf("error while reading list of log subpages, nothing to show\n");
		return;
	}

	if (buf_len < 4) {
		printf("log sense list must have at least 4 bytes\n");
		return;
	}

	if (buf[0] != 0x40 || buf[1] != 0xFF) {
		printf("expected to receive log page 0 (spf=1) subpage 0xFF\n");
		return;
	}

	num_pages = get_uint16(buf, 2);
	for (i = 0; i < num_pages; i++) {
		uint8_t page = buf[4 + i*2] & 0x3F;
		uint8_t subpage = buf[4 + i*2 + 1];
		if (subpage == 0) {
			printf("Skipping page %02X subpage %02X since subpage is 00 it was already retrieved above\n", page, subpage);
			continue;
		}
		dump_log_sense(fd, page, subpage);
	}
}

static void do_mode_sense_10_type(int fd, bool long_lba, bool disable_block_desc, page_control_e page_control)
{
	unsigned char cdb[32];
	unsigned char buf[4096];
	unsigned cdb_len = cdb_mode_sense_10(cdb, long_lba, disable_block_desc, page_control, 0x3F, 0xFF, sizeof(buf));

	memset(buf, 0, sizeof(buf));
	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);
	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
}

static void do_mode_sense_10(int fd)
{
	do_mode_sense_10_type(fd, true, true, PAGE_CONTROL_CURRENT);
	do_mode_sense_10_type(fd, true, true, PAGE_CONTROL_CHANGEABLE);
	do_mode_sense_10_type(fd, true, true, PAGE_CONTROL_DEFAULT);
	do_mode_sense_10_type(fd, true, true, PAGE_CONTROL_SAVED);

	do_mode_sense_10_type(fd, false, true, PAGE_CONTROL_CURRENT);
	do_mode_sense_10_type(fd, false, true, PAGE_CONTROL_CHANGEABLE);
	do_mode_sense_10_type(fd, false, true, PAGE_CONTROL_DEFAULT);
	do_mode_sense_10_type(fd, false, true, PAGE_CONTROL_SAVED);

	do_mode_sense_10_type(fd, false, false, PAGE_CONTROL_CURRENT);
	do_mode_sense_10_type(fd, false, false, PAGE_CONTROL_CHANGEABLE);
	do_mode_sense_10_type(fd, false, false, PAGE_CONTROL_DEFAULT);
	do_mode_sense_10_type(fd, false, false, PAGE_CONTROL_SAVED);

	do_mode_sense_10_type(fd, true, false, PAGE_CONTROL_CURRENT);
	do_mode_sense_10_type(fd, true, false, PAGE_CONTROL_CHANGEABLE);
	do_mode_sense_10_type(fd, true, false, PAGE_CONTROL_DEFAULT);
	do_mode_sense_10_type(fd, true, false, PAGE_CONTROL_SAVED);
}

static void do_mode_sense_6_type(int fd, bool disable_block_desc, page_control_e page_control)
{
	unsigned char cdb[32];
	unsigned char buf[255];
	unsigned cdb_len = cdb_mode_sense_6(cdb, disable_block_desc, page_control, 0x3F, 0xFF, sizeof(buf));

	memset(buf, 0, sizeof(buf));
	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);
	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
}

static void do_mode_sense_6(int fd)
{
	do_mode_sense_6_type(fd, true, PAGE_CONTROL_CURRENT);
	do_mode_sense_6_type(fd, true, PAGE_CONTROL_CHANGEABLE);
	do_mode_sense_6_type(fd, true, PAGE_CONTROL_DEFAULT);
	do_mode_sense_6_type(fd, true, PAGE_CONTROL_SAVED);

	do_mode_sense_6_type(fd, false, PAGE_CONTROL_CURRENT);
	do_mode_sense_6_type(fd, false, PAGE_CONTROL_CHANGEABLE);
	do_mode_sense_6_type(fd, false, PAGE_CONTROL_DEFAULT);
	do_mode_sense_6_type(fd, false, PAGE_CONTROL_SAVED);
}

static void do_mode_sense(int fd)
{
	do_mode_sense_10(fd);
	do_mode_sense_6(fd);
}

static void dump_rcv_diag_page(int fd, uint8_t page)
{
	unsigned char cdb[32];
	unsigned char buf[16*1024];
	unsigned cdb_len = cdb_receive_diagnostics(cdb, true, page, sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
}

static void do_receive_diagnostic(int fd)
{
	unsigned char cdb[32];
	unsigned char buf[16*1024];
	unsigned cdb_len = cdb_receive_diagnostics(cdb, true, 0, sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);

	if (sense) {
		printf("error while reading response buffer, nothing to show\n");
		return;
	}

	if (buf_len < RECV_DIAG_MIN_LEN) {
		printf("receive diagnostics list must have at least 4 bytes\n");
		return;
	}

	if (recv_diag_get_page_code(buf) != 0) {
		printf("expected to receive receive diagnostics page 0\n");
		return;
	}

	uint16_t num_pages = recv_diag_get_len(buf);
	uint16_t i;
	for (i = 0; i < num_pages; i++) {
		dump_rcv_diag_page(fd, buf[4 + i]);
	}
}

static void do_read_capacity_10(int fd)
{
	unsigned char cdb[32];
	unsigned char buf[8];
	unsigned cdb_len = cdb_read_capacity_10(cdb);

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
}

static void do_read_capacity_16(int fd)
{
	unsigned char cdb[32];
	unsigned char buf[512];
	unsigned cdb_len = cdb_read_capacity_16(cdb, sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
}

static void do_read_capacity(int fd)
{
	do_read_capacity_10(fd);
	do_read_capacity_16(fd);
}

static void do_read_defect_data_10(int fd, bool plist, bool glist, uint8_t format, bool count_only)
{
	unsigned char cdb[32];
	unsigned char buf[512];
	unsigned cdb_len = cdb_read_defect_data_10(cdb, plist, glist, format, count_only ? 8 : sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
}

static void do_read_defect_data_10_all(int fd, uint8_t format)
{
	do_read_defect_data_10(fd, true, false, format, true);
	do_read_defect_data_10(fd, true, false, format, false);
	do_read_defect_data_10(fd, false, true, format, true);
	do_read_defect_data_10(fd, false, true, format, false);
}

static void do_read_defect_data_12(int fd, bool plist, bool glist, uint8_t format, bool count_only)
{
	unsigned char cdb[32];
	unsigned char buf[512];
	unsigned cdb_len = cdb_read_defect_data_12(cdb, plist, glist, format, count_only ? 8 : sizeof(buf));

	memset(buf, 0, sizeof(buf));

	bool ret = submit_cmd(fd, cdb, cdb_len, buf, sizeof(buf), SG_DXFER_FROM_DEV);
	if (!ret) {
		fprintf(stderr, "Failed to submit command\n");
		return;
	}

	unsigned char *sense = NULL;
	unsigned sense_len = 0;
	unsigned buf_len = 0;
	ret = read_response_buf(fd, &sense, &sense_len, &buf_len);

	emit_data_csv(cdb, cdb_len, sense, sense_len, buf, buf_len);
}

static void do_read_defect_data_12_all(int fd, uint8_t format)
{
	do_read_defect_data_12(fd, true, false, format, true);
	do_read_defect_data_12(fd, true, false, format, false);
	do_read_defect_data_12(fd, false, true, format, true);
	do_read_defect_data_12(fd, false, true, format, false);
}

static void do_read_defect_data(int fd)
{
	uint8_t format;

	for (format = 0; format < 8; format++)
		do_read_defect_data_10_all(fd, format);

	for (format = 0; format < 8; format++)
		do_read_defect_data_12_all(fd, format);
}

void do_command(int fd)
{
	debug = 0;
	printf("msg,cdb,sense,data\n");
	do_read_capacity(fd);
	do_simple_inquiry(fd);
	do_extended_inquiry(fd);
	do_log_sense(fd);
	do_mode_sense(fd);
	do_receive_diagnostic(fd);
	do_read_defect_data(fd);
}