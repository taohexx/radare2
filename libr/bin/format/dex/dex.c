/* radare - LGPL - Copyright 2009-2019 - pancake, h4ng3r */

#include <r_types.h>
#include <r_util.h>
#include "dex.h"
#include "./dex_uleb128.inc"

char* r_bin_dex_get_version(RBinDexObj *bin) {
	r_return_val_if_fail (bin, NULL);
	char* version = calloc (1, 8);
	if (version) {
		r_buf_read_at (bin->b, 4, (ut8*)version, 3);
		return version;
	}
	return NULL;
}

static char *getstr(RBinDexObj *bin, int idx) {
	ut8 buf[6];
	ut64 len;
	int uleblen;
	// null terminate the buf wtf
	if (!bin || idx < 0 || idx >= bin->header.strings_size || !bin->strings) {
		return NULL;
	}
	if (bin->strings[idx] >= bin->size) {
		return NULL;
	}
	if (r_buf_read_at (bin->b, bin->strings[idx], buf, sizeof (buf)) < 1) {
		return NULL;
	}
	r_buf_write_at (bin->b, r_buf_size (bin->b) - 1, (ut8 *)"\x00", 1);
	uleblen = r_uleb128 (buf, sizeof (buf), &len) - buf;
	if (!uleblen || uleblen >= bin->size) {
		return NULL;
	}
	if (!len || len >= bin->size) {
		return NULL;
	}
	if (bin->strings[idx] + uleblen >= bin->strings[idx] + bin->header.strings_size) {
		return NULL;
	}
	ut8 *ptr = R_NEWS (ut8, len + 1);
	if (!ptr) {
		return NULL;
	}
	r_buf_read_at (bin->b, bin->strings[idx] + uleblen, ptr, len + 1);
	ptr[len] = 0;
	if (len != r_utf8_strlen (ptr)) {
		// eprintf ("WARNING: Invalid string for index %d\n", idx);
		return NULL;
	}
	return (char *)ptr;
}

static const char *visibility(int n) {
	switch (n) {
#if 0
	case kDexVisibilityBuild:
		return "build";
	case kDexVisibilityRuntime:
		return "runtime";
	case kDexVisibilitySystem:
		return "system";
#endif
	case kDexAnnotationByte:
	case kDexAnnotationShort:
		return "short";
	case kDexAnnotationChar          :
		return "char";
	case kDexAnnotationInt           :
		return "int";
	case kDexAnnotationLong          :
		return "long";
	case kDexAnnotationFloat         :
		return "float";
	case kDexAnnotationDouble        :
		return "double";
	case kDexAnnotationString        :
		return "string";
	case kDexAnnotationType          :
		return "type";
	case kDexAnnotationField         :
		return "field";
	case kDexAnnotationMethod        :
		return "method";
	case kDexAnnotationEnum          :
		return "enum";
	case kDexAnnotationArray         :
		return "array";
	case kDexAnnotationAnnotation    :
		return "annotation";
	case kDexAnnotationNull          :
		return "null";
	case kDexAnnotationBoolean       :
		return "bool";
	}
	return "?";
}

static const char *className (RBinDexObj *bin, int idx) {
	DexType *dt = &bin->types[idx];
	char *name = getstr (bin, dt->descriptor_id); // bin->classes[i].name_id);
	return name;
}

#define FAIL(x) { eprintf(x"\n"); goto fail; }

RBinDexObj *r_bin_dex_new_buf(RBuffer *buf) {
	r_return_val_if_fail (buf, NULL);
	RBinDexObj *bin = R_NEW0 (RBinDexObj);
	int i;
	struct dex_header_t *dexhdr;
	if (!bin) {
		goto fail;
	}
	bin->size = r_buf_size (buf);
	bin->b = r_buf_ref (buf);
	/* header */
	if (bin->size < sizeof (struct dex_header_t)) {
		goto fail;
	}
	dexhdr = &bin->header;

	if (bin->size < 112) {
		goto fail;
	}

	r_buf_seek (bin->b, 0, R_BUF_SET);
	r_buf_read (bin->b, (ut8 *)&dexhdr->magic, 8);
	dexhdr->checksum = r_buf_read_le32 (bin->b);
	r_buf_read (bin->b, (ut8 *)&dexhdr->signature, 20);
	dexhdr->size = r_buf_read_le32 (bin->b);
	dexhdr->header_size = r_buf_read_le32 (bin->b);
	dexhdr->endian = r_buf_read_le32 (bin->b);
	// TODO: this offsets and size will be used for checking,
	// so they should be checked. Check overlap, < 0, > bin.size
	dexhdr->linksection_size = r_buf_read_le32 (bin->b);
	dexhdr->linksection_offset = r_buf_read_le32 (bin->b);
	dexhdr->map_offset = r_buf_read_le32 (bin->b);
	dexhdr->strings_size = r_buf_read_le32 (bin->b);
	dexhdr->strings_offset = r_buf_read_le32 (bin->b);
	dexhdr->types_size = r_buf_read_le32 (bin->b);
	dexhdr->types_offset = r_buf_read_le32 (bin->b);
	dexhdr->prototypes_size = r_buf_read_le32 (bin->b);
	dexhdr->prototypes_offset = r_buf_read_le32 (bin->b);
	dexhdr->fields_size = r_buf_read_le32 (bin->b);
	dexhdr->fields_offset = r_buf_read_le32 (bin->b);
	dexhdr->method_size = r_buf_read_le32 (bin->b);
	dexhdr->method_offset = r_buf_read_le32 (bin->b);
	dexhdr->class_size = r_buf_read_le32 (bin->b);
	dexhdr->class_offset = r_buf_read_le32 (bin->b);
	dexhdr->data_size = r_buf_read_le32 (bin->b);
	dexhdr->data_offset = r_buf_read_le32 (bin->b);

	/* strings */
	#define STRINGS_SIZE ((dexhdr->strings_size + 1) * sizeof (ut32))
	bin->strings = (ut32 *) calloc (dexhdr->strings_size + 1, sizeof (ut32));
	if (!bin->strings) {
		goto fail;
	}
	if (dexhdr->strings_size > bin->size) {
		free (bin->strings);
		goto fail;
	}
	for (i = 0; i < dexhdr->strings_size; i++) {
		ut64 offset = dexhdr->strings_offset + i * sizeof (ut32);
		if (offset + 4 > bin->size) {
			free (bin->strings);
			goto fail;
		}
		bin->strings[i] = r_buf_read_le32_at (bin->b, offset);
	}
	/* classes */
	// TODO: not sure about if that is needed
	int classes_size = dexhdr->class_size * DEX_CLASS_SIZE;
	if (dexhdr->class_offset + classes_size >= bin->size) {
		classes_size = bin->size - dexhdr->class_offset;
	}
	if (classes_size < 0) {
		classes_size = 0;
	}

	dexhdr->class_size = classes_size / DEX_CLASS_SIZE;
	bin->classes = (struct dex_class_t *) calloc (dexhdr->class_size, sizeof (struct dex_class_t));
	for (i = 0; i < dexhdr->class_size; i++) {
		ut64 offset = dexhdr->class_offset + i * DEX_CLASS_SIZE;
		if (offset + 32 > bin->size) {
			free (bin->strings);
			free (bin->classes);
			goto fail;
		}
		r_buf_seek (bin->b, offset, R_BUF_SET);
		bin->classes[i].class_id = r_buf_read_le32 (bin->b);
		bin->classes[i].access_flags = r_buf_read_le32 (bin->b);
		bin->classes[i].super_class = r_buf_read_le32 (bin->b);
		bin->classes[i].interfaces_offset = r_buf_read_le32 (bin->b);
		bin->classes[i].source_file = r_buf_read_le32 (bin->b);
		bin->classes[i].anotations_offset = r_buf_read_le32 (bin->b);
		bin->classes[i].class_data_offset = r_buf_read_le32 (bin->b);
		bin->classes[i].static_values_offset = r_buf_read_le32 (bin->b);
	}

	/* methods */
	int methods_size = dexhdr->method_size * sizeof (struct dex_method_t);
	if (dexhdr->method_offset + methods_size >= bin->size) {
		methods_size = bin->size - dexhdr->method_offset;
	}
	if (methods_size < 0) {
		methods_size = 0;
	}
	dexhdr->method_size = methods_size / sizeof (struct dex_method_t);
	bin->methods = (struct dex_method_t *) calloc (methods_size + 1, 1);
	for (i = 0; i < dexhdr->method_size; i++) {
		ut64 offset = dexhdr->method_offset + i * sizeof (struct dex_method_t);
		if (offset + 8 > bin->size) {
			free (bin->strings);
			free (bin->classes);
			free (bin->methods);
			goto fail;
		}
		r_buf_seek (bin->b, offset, R_BUF_SET);
		bin->methods[i].class_id = r_buf_read_le16 (bin->b);
		bin->methods[i].proto_id = r_buf_read_le16 (bin->b);
		bin->methods[i].name_id = r_buf_read_le32 (bin->b);
	}

	/* types */
	int types_size = dexhdr->types_size * sizeof (struct dex_type_t);
	if (dexhdr->types_offset + types_size >= bin->size) {
		types_size = bin->size - dexhdr->types_offset;
	}
	if (types_size < 0) {
		types_size = 0;
	}
	dexhdr->types_size = types_size / sizeof (struct dex_type_t);
	bin->types = (struct dex_type_t *) calloc (types_size + 1, 1);
	for (i = 0; i < dexhdr->types_size; i++) {
		ut64 offset = dexhdr->types_offset + i * sizeof (struct dex_type_t);
		if (offset + 4 > bin->size) {
			free (bin->strings);
			free (bin->classes);
			free (bin->methods);
			free (bin->types);
			goto fail;
		}
		bin->types[i].descriptor_id = r_buf_read_le32_at (bin->b, offset);
	}

	/* fields */
	int fields_size = dexhdr->fields_size * sizeof (struct dex_field_t);
	if (dexhdr->fields_offset + fields_size >= bin->size) {
		fields_size = bin->size - dexhdr->fields_offset;
	}
	if (fields_size < 0) {
		fields_size = 0;
	}
	dexhdr->fields_size = fields_size / sizeof (struct dex_field_t);
	bin->fields = (struct dex_field_t *) calloc (fields_size + 1, 1);
	for (i = 0; i < dexhdr->fields_size; i++) {
		ut64 offset = dexhdr->fields_offset + i * sizeof (struct dex_field_t);
		if (offset + 8 > bin->size) {
			free (bin->strings);
			free (bin->classes);
			free (bin->methods);
			free (bin->types);
			free (bin->fields);
			goto fail;
		}
		r_buf_seek (bin->b, offset, R_BUF_SET);
		bin->fields[i].class_id = r_buf_read_le16 (bin->b);
		bin->fields[i].type_id = r_buf_read_le16 (bin->b);
		bin->fields[i].name_id = r_buf_read_le32 (bin->b);
	}

	/* proto */
	int protos_size = dexhdr->prototypes_size * sizeof (struct dex_proto_t);
	if (dexhdr->prototypes_offset + protos_size >= bin->size) {
		protos_size = bin->size - dexhdr->prototypes_offset;
	}
	if (protos_size < 1) {
		dexhdr->prototypes_size = 0;
		return bin;
	}
	dexhdr->prototypes_size = protos_size / sizeof (struct dex_proto_t);
	bin->protos = (struct dex_proto_t *) calloc (protos_size, 1);
	for (i = 0; i < dexhdr->prototypes_size; i++) {
		ut64 offset = dexhdr->prototypes_offset + i * sizeof (struct dex_proto_t);
		if (offset + 12 > bin->size) {
			free (bin->strings);
			free (bin->classes);
			free (bin->methods);
			free (bin->types);
			free (bin->fields);
			free (bin->protos);
			goto fail;
		}
		r_buf_seek (bin->b, offset, R_BUF_SET);
		bin->protos[i].shorty_id = r_buf_read_le32 (bin->b);
		bin->protos[i].return_type_id = r_buf_read_le32 (bin->b);
		bin->protos[i].parameters_off = r_buf_read_le32 (bin->b);
	}
	eprintf ("Parse annotations\n");
	for (i = 0; i < dexhdr->class_size; i++) {
		ut64 at = bin->classes[i].anotations_offset;
		if (at == UT64_MAX || at == 0LL) {
			eprintf ("nope\n");
			continue;
		}
		int j;
		const char *cn = className (bin, bin->classes[i].class_id);
		for (j = 0; j < 2; j++) {
			r_buf_seek (bin->b, at, R_BUF_SET);
			ut64 v, v2;
			ut64 count;
			int len = r_buf_uleb128 (bin->b, &count);
count = len;
			int k;
			if (count > 0) {
eprintf ("COUNT (%s) %lld %x\n", cn, count, len);
				for (k = 0; k < count; k++) {
					r_buf_uleb128 (bin->b, &v2);
					if (v2== UT64_MAX) {
						break;
					}
					const char *type = visibility (v2 & 0x1f);
					//if (v2 == kDexAnnotationType) {
						ut64 fo = 0; r_buf_uleb128 (bin->b, &fo);
						char *an = getstr (bin, fo);
						eprintf ("Few %lld %lld %s\n", fo, 0, an);
					//}
					eprintf ("%d %llx %s", k, v2, type);
				}
				eprintf ("\n");
break;
			} else {
break;
			}
#if 0
			ut64 k2 = r_buf_uleb128 (bin->b, &v2);
			eprintf ("K %llx %llx\n", k, v);
			eprintf ("V %llx %llx\n", k2, v2);
			r_buf_seek (bin->b, at, R_BUF_SET);
			ut32 a = r_buf_read_le32 (bin->b);
			ut32 b = r_buf_read_le32 (bin->b);
			if (!a && !b) {
				break;
			}
			DexType *dt = &bin->types[bin->classes[i].class_id];
			char *name = getstr (bin, dt->descriptor_id); // bin->classes[i].name_id);
			const char *vt = visibility (a & 0x1f);
			DexType *dt2 = &bin->types[a&0x1f];
			const char *vt2 = getstr (bin, v2);
			eprintf ("0x%08llx  %x %x %10s %s %s\n", at, (a >> 5), b, vt, vt2, name);
#endif
			at += 8;
		}
break;
	}

	return bin;
fail:
	if (bin) {
		r_buf_free (bin->b);
		free (bin);
	}
	return NULL;
}
