/*
 * Parser for TRX format partitions
 *
 * Copyright (C) 2012 - 2017 Rafał Miłecki <rafal@milecki.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#define TRX_PARSER_MAX_PARTS		4

/* Magics */
#define TRX_MAGIC			0x30524448
#define UBI_EC_MAGIC			0x23494255	/* UBI# */

struct trx_header {
	uint32_t magic;
	uint32_t length;
	uint32_t crc32;
	uint16_t flags;
	uint16_t version;
	uint32_t offset[3];
} __packed;

/*
 * Calculate real end offset (address) for a given amount of data. It checks
 * all blocks skipping bad ones.
 */
static size_t parser_trx_real_offset(struct mtd_info *mtd, size_t bytes)
{
	size_t real_offset = 0;

	if (mtd_block_isbad(mtd, real_offset))
		pr_warn("Base offset shouldn't be at bad block");

	while (bytes >= mtd->erasesize) {
		bytes -= mtd->erasesize;
		real_offset += mtd->erasesize;
		while (mtd_block_isbad(mtd, real_offset)) {
			real_offset += mtd->erasesize;

			if (real_offset >= mtd->size)
				return real_offset - mtd->erasesize;
		}
	}

	real_offset += bytes;

	return real_offset;
}

static const char *parser_trx_data_part_name(struct mtd_info *master,
					     size_t offset)
{
	uint32_t buf;
	size_t bytes_read;
	int err;

	err  = mtd_read(master, offset, sizeof(buf), &bytes_read,
			(uint8_t *)&buf);
	if (err && !mtd_is_bitflip(err)) {
		pr_err("mtd_read error while parsing (offset: 0x%X): %d\n",
			offset, err);
		goto out_default;
	}

	if (buf == UBI_EC_MAGIC)
		return "ubi";

out_default:
	return "rootfs";
}

static int parser_trx_parse(struct mtd_info *mtd,
			    const struct mtd_partition **pparts,
			    struct mtd_part_parser_data *data)
{
	struct mtd_partition *parts;
	struct mtd_partition *part;
	struct trx_header trx;
	size_t bytes_read;
	uint8_t curr_part = 0, i = 0;
	int err;

	parts = kzalloc(sizeof(struct mtd_partition) * TRX_PARSER_MAX_PARTS,
			GFP_KERNEL);
	if (!parts)
		return -ENOMEM;

	err = mtd_read(mtd, 0, sizeof(trx), &bytes_read, (uint8_t *)&trx);
	if (err) {
		pr_err("MTD reading error: %d\n", err);
		kfree(parts);
		return err;
	}

	if (trx.magic != TRX_MAGIC) {
		kfree(parts);
		return -ENOENT;
	}

	/* We have LZMA loader if there is address in offset[2] */
	if (trx.offset[2]) {
		part = &parts[curr_part++];
		part->name = "loader";
		part->offset = parser_trx_real_offset(mtd, trx.offset[i]);
		i++;
	}

	if (trx.offset[i]) {
		part = &parts[curr_part++];
		part->name = "linux";
		part->offset = parser_trx_real_offset(mtd, trx.offset[i]);
		i++;
	}

	if (trx.offset[i]) {
		part = &parts[curr_part++];
		part->offset = parser_trx_real_offset(mtd, trx.offset[i]);
		part->name = parser_trx_data_part_name(mtd, part->offset);
		i++;
	}

	/*
	 * Assume that every partition ends at the beginning of the one it is
	 * followed by.
	 */
	for (i = 0; i < curr_part; i++) {
		u64 next_part_offset = (i < curr_part - 1) ?
				       parts[i + 1].offset : mtd->size;

		parts[i].size = next_part_offset - parts[i].offset;
	}

	*pparts = parts;
	return i;
};

static struct mtd_part_parser mtd_parser_trx = {
	.parse_fn = parser_trx_parse,
	.name = "trx",
};
module_mtd_part_parser(mtd_parser_trx);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Parser for TRX format partitions");
