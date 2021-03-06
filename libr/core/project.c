/* radare - LGPL - Copyright 2010-2015 - pancake */

#include <r_types.h>
#include <r_list.h>
#include <r_flags.h>
#include <r_core.h>

static int is_valid_project_name (const char *name) {
	int i;
	for (i=0; name[i]; i++) {
		switch (name[i]) {
		case '_':
		case ':':
			continue;
		}
		if (name[i] >= 'a' && name[i] <= 'z')
			continue;
		if (name[i] >= 'A' && name[i] <= 'Z')
			continue;
		if (name[i] >= '0' && name[i] <= '9')
			continue;
		return 0;
	}
	return 1;
}

static char *r_core_project_file(RCore *core, const char *file) {
	const char *magic = "# r2 rdb project file";
	char *data, *prjfile;
	if (*file != '/') {
		if (!is_valid_project_name (file))
			return NULL;
		prjfile = r_file_abspath (r_config_get (
			core->config, "dir.projects"));
		prjfile = r_str_concat (prjfile, "/");
		prjfile = r_str_concat (prjfile, file);
	} else prjfile = strdup (file);
	data = r_file_slurp (prjfile, NULL);
	if (data) {
		if (strncmp (data, magic, strlen (magic))) {
			R_FREE (prjfile);
		}
	}
	free (data);
	return prjfile;
}

static int r_core_project_init(RCore *core) {
	char *prjdir = r_file_abspath (r_config_get (
		core->config, "dir.projects"));
	int ret = r_sys_rmkdir (prjdir);
	if (!ret) eprintf ("Cannot mkdir dir.projects\n");
	free (prjdir);
	return ret;
}

static int r_core_is_project(RCore *core, const char *name) {
	int ret = 0;
	if (name && *name && *name!='.') {
		char *path = r_core_project_file (core, name);
		if (!path)
			return 0;
		path = r_str_concat (path, ".d");
		if (r_file_is_directory (path))
			ret = 1;
		free (path);
	}
	return ret;
}

R_API int r_core_project_cat(RCore *core, const char *name) {
	char *path = r_core_project_file (core, name);
	if (path) {
		char *data = r_file_slurp (path, NULL);
		if (data) {
			r_cons_printf ("%s\n", data);
			free (data);
		}
	}
	free (path);
	return 0;
}

R_API int r_core_project_list(RCore *core, int mode) {
	RListIter *iter;
	RList *list;
	int isfirst = 1;
	char *foo, *path = r_file_abspath (r_config_get (core->config, "dir.projects"));
	if (!path)
		return 0;
	list = r_sys_dir (path);
	switch (mode) {
	case 'j':
		r_cons_printf ("[");
		r_list_foreach (list, iter, foo) {
			// todo. escape string
			if (r_core_is_project (core, foo)) {
				r_cons_printf ("%s\"%s\"",
					isfirst?"":",", foo);
				isfirst = 0;
			}
		}
		r_cons_printf ("]\n");
		break;
	default:
		r_list_foreach (list, iter, foo) {
			if (r_core_is_project (core, foo))
				r_cons_printf ("%s\n", foo);
		}
		break;
	}
	r_list_free (list);
	free (path);
	return 0;
}

R_API int r_core_project_delete(RCore *core, const char *prjfile) {
	char *path;
	if (r_sandbox_enable (0)) {
		eprintf ("Cant delete project in sandbox mode\n");
		return 0;
	}
	path = r_core_project_file (core, prjfile);
	if (!path) {
		eprintf ("Invalid project name '%s'\n", prjfile);
		return R_FALSE;
	}
	if (r_core_is_project (core, prjfile)) {
		// rm project file
		r_file_rm (path);
		eprintf ("rm %s\n", path);
		path = r_str_concat (path, ".d");
		if (r_file_is_directory (path)) {
			char *f;
			RListIter *iter;
			RList *files = r_sys_dir (path);
			r_list_foreach (files, iter, f) {
				char *filepath = r_str_concat (strdup (path), "/");
				filepath =r_str_concat (filepath, f);
				if (!r_file_is_directory (filepath)) {
					eprintf ("rm %s\n", filepath);
					r_file_rm (filepath);
				}
				free (filepath);
			}
			r_file_rm (path);
			eprintf ("rm %s\n", path);
			r_list_free (files);
		}
		// TODO: remove .d directory (BEWARE OF ROOT RIMRAFS!)
		// TODO: r_file_rmrf (path);
	}
	free (path);
	return 0;
}

R_API int r_core_project_open(RCore *core, const char *prjfile) {
	int askuser = 1;
	int ret, close_current_session = 1;
	char *prj, *filepath;
	if (!prjfile || !*prjfile)
		return R_FALSE;
	prj = r_core_project_file (core, prjfile);
	if (!prj) {
		eprintf ("Invalid project name '%s'\n", prjfile);
		return R_FALSE;
	}
	filepath = r_core_project_info (core, prj);
	//eprintf ("OPENING (%s) from %s\n", prj, r_config_get (core->config, "file.path"));
	/* if it is not an URI */
	if (!filepath) {
		eprintf ("Cannot retrieve information for project '%s'\n", prj);
		free (prj);
		return R_FALSE;
	}
	if (!strstr (filepath, "://")) {
		/* check if path exists */
		if (!r_file_exists (filepath)) {
			eprintf ("Cannot find file '%s'\n", filepath);
			free (prj);
			free (filepath);
			return R_FALSE;
		}
	}
	if (!strcmp (prjfile, r_config_get (core->config, "file.project"))) {
		eprintf ("Reloading project\n");
		askuser = 0;
#if 0
		free (prj);
		free (filepath);
		return R_FALSE;
#endif
	}
	if (askuser) {
		if (r_config_get_i (core->config, "scr.interactive")) {
			close_current_session = r_cons_yesno ('y', "Close current session? (Y/n)");
		}
	}
	if (close_current_session) {
		RCoreFile *fh;
		// delete
		r_core_file_close_fd (core, -1);
		r_io_close_all (core->io);
		r_anal_purge (core->anal);
		r_flag_unset_all (core->flags);
		r_bin_file_delete_all (core->bin);
		// open new file
		// TODO: handle read/read-write mode
		// TODO: handle mapaddr (io.maps are not saved in projects yet)
		fh = r_core_file_open (core, filepath, 0, 0);
		if (!fh) {
			eprintf ("Cannot open file '%s'\n", filepath);
			free (filepath);
			free (prj);
			return R_FALSE;
		}
		// TODO: handle load bin info or not
		// TODO: handle base address
		r_core_bin_load (core, filepath, UT64_MAX);
	}
	ret = r_core_cmd_file (core, prj);
	r_anal_project_load (core->anal, prjfile);
	r_core_cmd0 (core, "s entry0");
	free (filepath);
	free (prj);
	return ret;
}

R_API char *r_core_project_info(RCore *core, const char *prjfile) {
	FILE *fd;
	char buf[256], *file = NULL, *prj = r_core_project_file (core, prjfile);
	if (!prj) {
		eprintf ("Invalid project name '%s'\n", prjfile);
		return NULL;
	}
	fd = r_sandbox_fopen (prj, "r");
	for (;fd;) {
		fgets (buf, sizeof (buf), fd);
		if (feof (fd))
			break;
		if (!memcmp (buf, "\"e file.path = ", 15)) {
			buf[strlen(buf)-2]=0;
			file = r_str_new (buf+15);
			break;
		}
		// TODO: deprecate before 1.0
		if (!memcmp (buf, "e file.path = ", 14)) {
			buf[strlen(buf)-1]=0;
			file = r_str_new (buf+14);
			break;
		}
	}
	if (fd) fclose (fd);
	r_cons_printf ("%s\n", prj);
	if (file) r_cons_printf ("FilePath: %s\n", file);
	free (prj);
	return file;
}

R_API int r_core_project_save(RCore *core, const char *file) {
	int fd, fdold, tmp, ret = R_TRUE;
	char *prj;

	if (file == NULL || *file == '\0')
		return R_FALSE;

	prj = r_core_project_file (core, file);
	if (!prj) {
		eprintf ("Invalid project name '%s'\n", file);
		return R_FALSE;
	}
	if (r_file_is_directory (prj)) {
		eprintf ("Error: Target is a directory\n");
		free (prj);
		return R_FALSE;
	}
	r_core_project_init (core);
	r_anal_project_save (core->anal, prj);
	fd = r_sandbox_open (prj, O_BINARY|O_RDWR|O_CREAT|O_TRUNC, 0644);
	if (fd != -1) {
		fdold = r_cons_singleton ()->fdout;
		r_cons_singleton ()->fdout = fd;
		r_cons_singleton ()->is_interactive = R_FALSE;
		r_str_write (fd, "# r2 rdb project file\n");
		r_str_write (fd, "# flags\n");
		tmp = core->flags->space_idx;
		core->flags->space_idx = -1;
		r_flag_list (core->flags, R_TRUE, NULL);
		core->flags->space_idx = tmp;
		r_cons_flush ();
		r_str_write (fd, "# eval\n");
		// TODO: r_str_writef (fd, "e asm.arch=%s", r_config_get ("asm.arch"));
		r_config_list (core->config, NULL, R_TRUE);
		r_cons_flush ();
		r_str_write (fd, "# sections\n");
		r_io_section_list (core->io, core->offset, 1);
		r_cons_flush ();
		r_str_write (fd, "# meta\n");
		r_meta_list (core->anal, R_META_TYPE_ANY, 1);
		r_cons_flush ();
		 {
			char buf[1024];
			snprintf (buf, sizeof (buf), "%s.d/xrefs", prj);
			sdb_file (core->anal->sdb_xrefs, buf);
			sdb_sync (core->anal->sdb_xrefs);
		 }
		r_core_cmd (core, "ax*", 0);
		r_cons_flush ();
		r_core_cmd (core, "af*", 0);
		r_cons_flush ();
		r_core_cmd (core, "ah*", 0);
		r_cons_flush ();
		r_cons_printf ("# seek\n"
			"s 0x%08"PFMT64x"\n", core->offset);
		r_cons_flush ();
		close (fd);
		r_cons_singleton ()->fdout = fdold;
		r_cons_singleton ()->is_interactive = R_TRUE;
	} else {
		eprintf ("Cannot open '%s' for writing\n", prj);
		ret = R_FALSE;
	}
	free (prj);
	return ret;
}

R_API char *r_core_project_notes_file (RCore *core, const char *file) {
	char *notes_txt;
	const char *prjdir = r_config_get (core->config, "dir.projects");
	char *prjpath = r_file_abspath (prjdir);
	notes_txt = r_str_newf ("%s/%s.d/notes.txt", prjpath, file);
	free (prjpath);
	return notes_txt;
}
