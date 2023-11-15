#include <assert.h>

#include "jstream.h"
#include "utils.h"

static const char indent[] = "    ";

int
jstream_open(struct jstream *jstream, const char *file_name)
{
	jstream->lvl = 0;
	jstream->f = fopen(file_name, "w");
	if (jstream->f == NULL)
		return -1;
	if (fprintf(jstream->f, "[\n") < 0) {
		fclose(jstream->f);
		return -1;
	}
	jstream->is_first = true;
	return 0;
}

int
jstream_write_map(struct jstream *jstream, const char **keys,
		  const char **values, size_t num)
{
	const char *map_open = "{\n";
	const char *map_close = "}";
	const char *close_prev = ",\n";
	if (jstream->is_first) {
		jstream->is_first = false;
		close_prev = "";
	}
	if (fprintf(jstream->f, "%s%s%s", close_prev, indent, map_open) < 0)
		return -1;
	jstream->lvl++;

	const char *sep = ",";
	for (size_t i = 0; i < num; i++) {
		if (i == num - 1)
			sep = "";
		if (fprintf(jstream->f, "%s%s\"%s\": \"%s\"%s\n", indent,
			    indent, keys[i], values[i], sep) < 0)
			return -1;
	}

	jstream->lvl--;
	if (fprintf(jstream->f, "%s%s", indent, map_close) < 0)
		return -1;
	return 0;
}

int
jstream_close(struct jstream *jstream)
{
	int rc = fprintf(jstream->f, "\n]\n");
	int close_rc = fclose(jstream->f);
	if (rc == 0 && close_rc == 0)
		return 0;
	return -1;
}
