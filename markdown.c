/**********************************************************************

  markdown.c - markdown in C using a PEG grammar.
  (c) 2008 John MacFarlane (jgm at berkeley dot edu).
  
  portions Copyright (c) 2010-2011 Fletcher T. Penney

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License or the MIT
  license.  See LICENSE for details.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

 ***********************************************************************/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <glib.h>
#include "markdown_peg.h"

static int extensions;

/**********************************************************************

  The main program is just a wrapper around the library functions in
  markdown_lib.c.  It parses command-line options, reads the text to
  be converted from input files or stdin, converts the text, and sends
  the output to stdout or a file.  Character encodings are ignored.

 ***********************************************************************/

#define VERSION "3.0.dev"
#define COPYRIGHT "portions Copyright (c) 2010-2011 Fletcher T. Penney.\n" \
                  "original Copyright (c) 2008-2009 John MacFarlane.  License GPLv2+ or MIT.\n" \
                  "This is free software: you are free to change and redistribute it.\n" \
                  "There is NO WARRANTY, to the extent permitted by law."

/* print version and copyright information */
void version(const char *progname)
{
  printf("peg-multimarkdown version %s\n"
         "%s\n",
         VERSION,
         COPYRIGHT);
}

// We like to use getopt on the standalone Mac version, because 
// it rids us of dependency on GLib's GOptions parser. But you 
// might want to use this on other platforms that support getopt, too.
#ifndef MD_USE_GET_OPT
#if STANDALONE_MAC_VERSION
#define MD_USE_GET_OPT	1
#else
#define MD_USE_GET_OPT	0
#endif
#endif

// These are a bit hacky - note that the supplied arguments are not used to the fullest in the getopt case. For 
// example we aren't able to use the specified description or in the case of strings, any specified output
// variables for the resulting string, we have to just catch those at processing time and create the strings.
#if MD_USE_GET_OPT
#include <getopt.h>
#define MD_ARGUMENT_FLAG(name, flagChar, flagValue, outPointer, desc, argPlaceholder)	{ name, no_argument, outPointer, outPointer ? flagValue : flagChar }
#define MD_ARGUMENT_STRING(name, flagChar, outPointer, desc, argPlaceholder)	{ name, required_argument, NULL, flagChar }
#else
#define MD_ARGUMENT_FLAG(name, flagChar, outPointer, desc, argPlaceholder)	{ name, flagChar, 0, G_OPTION_ARG_NONE, outPointer, desc, argPlaceholder }
#define MD_ARGUMENT_STRING(name, flagChar, outPointer, desc, argPlaceholder)	{ name, flagChar, 0, G_OPTION_ARG_STRING, outPointer, desc, argPlaceholder }
#endif

// With getopt we don't get the same fancy automatic usage (I don't think?) so for 
// now we're faking it ...
static void printUsage() {
	printf("Usage:\
  multimarkdown [OPTION...] [FILE...]\n\
\n\
Help Options:\n\
  -h, --help              Show help options\n\
\n\
Application Options:\n\
  -v, --version           print version and exit\n\
  -o, --output=FILE       send output to FILE (default is stdout)\n\
  -t, --to=FORMAT         convert to FORMAT (default is html)\n\
  -x, --extensions        use all syntax extensions\n\
  --filter-html           filter out raw HTML (except styles)\n\
  --filter-styles         filter out HTML styles\n\
  -c, --compatibility     markdown compatibility mode\n\
  -b, --batch             process multiple files automatically\n\
  -e, --extract           extract and display specified metadata\n\
\n\
Syntax extensions\n\
  --smart --nosmart       toggle smart typography extension\n\
  --notes --nonotes       toggle notes extension\n\
  --process-html          process MultiMarkdown inside of raw HTML\n\
\n\
Converts text in specified files (or stdin) from markdown to FORMAT.\n\
Available FORMATs:  html, latex, memoir, beamer, odf, opml\n");
}

int main(int argc, char * argv[]) {

#if STANDALONE_MAC_VERSION
	NSAutoreleasePool* toolPool = [[NSAutoreleasePool alloc] init];
#endif
	
    int numargs;            /* number of filename arguments */
    int i;

    GString *inputbuf;
    char *out;              /* string containing processed output */

    GString *file;
    char *fake;
    FILE *input;
    FILE *output;
    char curchar;
    char *progname = argv[0];

    int output_format = HTML_FORMAT;

    /* Code for command-line option parsing. */

    static gboolean opt_version = FALSE;
    static gchar *opt_output = 0;
    static gchar *opt_to = 0;
    static gboolean opt_smart = TRUE;
    static gboolean opt_no_smart = FALSE;
    static gboolean opt_notes = TRUE;
    static gboolean opt_no_notes = FALSE;
    static gboolean opt_process_html = FALSE;
    static gboolean opt_filter_html = FALSE;
    static gboolean opt_filter_styles = FALSE;
    static gboolean opt_allext = FALSE;
    static gboolean opt_compatibility = FALSE;
    static gboolean opt_batchmode = FALSE;
    static gchar *opt_extract_meta = FALSE;

#if MD_USE_GET_OPT
	static struct option entries[] =
	{
	  MD_ARGUMENT_FLAG( "help", 'h', 1, NULL, "Show help options", NULL ),
#else
	static GOptionEntry entries[] =
	{	
#endif	
	  MD_ARGUMENT_FLAG( "version", 'v', 1, &opt_version, "print version and exit", NULL ),
      MD_ARGUMENT_STRING( "output", 'o', &opt_output, "send output to FILE (default is stdout)", "FILE" ),
      MD_ARGUMENT_STRING( "to", 't', &opt_to, "convert to FORMAT (default is html)", "FORMAT" ),
      MD_ARGUMENT_FLAG( "extensions", 'x', 1, &opt_allext, "use all syntax extensions", NULL ),
      MD_ARGUMENT_FLAG( "filter-html", 0, 1, &opt_filter_html, "filter out raw HTML (except styles)", NULL ),
      MD_ARGUMENT_FLAG( "filter-styles", 0, 1, &opt_filter_styles, "filter out HTML styles", NULL ),
      MD_ARGUMENT_FLAG( "compatibility", 'c', 1, &opt_compatibility, "markdown compatibility mode", NULL ),
      MD_ARGUMENT_FLAG( "batch", 'b', 1, &opt_batchmode, "process multiple files automatically", NULL ),
      MD_ARGUMENT_STRING( "extract", 'e', &opt_extract_meta, "extract and display specified metadata", NULL ),
// For GOptions, the arguments are split into two groups
#if !MD_USE_GET_OPT
      { NULL }	
	};
	
    /* Options to active syntax extensions.  These appear separately in --help. */
    static GOptionEntry ext_entries[] =
    {
#endif
      MD_ARGUMENT_FLAG( "smart", 0, 1, &opt_smart, "use smart typography extension (on by default)", NULL ),
      MD_ARGUMENT_FLAG( "nosmart", 0, 1, &opt_no_smart, "do not use smart typography extension", NULL ),
      MD_ARGUMENT_FLAG( "notes", 0, 1, &opt_notes, "use notes extension (on by default)", NULL ),
      MD_ARGUMENT_FLAG( "nonotes", 0, 1, &opt_no_notes, "do not use notes extension", NULL ),
      MD_ARGUMENT_FLAG( "process-html", 0, 1, &opt_process_html, "process MultiMarkdown inside of raw HTML", NULL ),
      { NULL }
    };

#if MD_USE_GET_OPT
	char ch;
	while ((ch = getopt_long(argc, argv, "hvo:t:xcbe:", entries, NULL)) != -1) {
		 switch (ch) {
			case 'h':
				printUsage();
				return EXIT_SUCCESS;
				break;
			case 'o':
				opt_output = malloc(strlen(optarg) + 1);
				strcpy(opt_output, optarg);
				break;
			case 't':
				opt_to = malloc(strlen(optarg) + 1);
				strcpy(opt_to, optarg);
				break;
			case 'e':
				opt_extract_meta = malloc(strlen(optarg) + 1);
				strcpy(opt_extract_meta, optarg);
				break;
		 }
	}

	 argc -= optind;
	 argv += optind;		 
	
	// We expect argc and argv to still point just one below the start of remaining args
	argc++;
	argv--;
	
#else
    GError *error = NULL;
    GOptionContext *context;
    GOptionGroup *ext_group;

    context = g_option_context_new ("[FILE...]");
    g_option_context_add_main_entries (context, entries, NULL);
    ext_group = g_option_group_new ("extensions", "Syntax extensions", "show available syntax extensions", NULL, NULL);
    g_option_group_add_entries (ext_group, ext_entries);
    g_option_context_add_group (context, ext_group);
    g_option_context_set_description (context, "Converts text in specified files (or stdin) from markdown to FORMAT.\n"
                                               "Available FORMATs:  html, latex, memoir, beamer, odf, opml");
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_print ("option parsing failed: %s\n", error->message);
        exit (1);
    }
    g_option_context_free(context);

#endif

    /* Process command-line options and arguments. */

    if (opt_version) {
        version(progname);
        return EXIT_SUCCESS;
    }

    extensions = 0;
    if (opt_allext)
        extensions = 0xFFFFFF;  /* turn on all extensions */
    if (opt_no_smart)
        opt_smart = FALSE;
    if (opt_smart)
        extensions = extensions | EXT_SMART;
    if (opt_no_notes)
        opt_notes = FALSE;
    if (opt_notes)
        extensions = extensions | EXT_NOTES;
    if (opt_process_html)
        extensions = extensions | EXT_PROCESS_HTML;
    if (opt_filter_html)
        extensions = extensions | EXT_FILTER_HTML;
    if (opt_filter_styles)
        extensions = extensions | EXT_FILTER_STYLES;

    /* Compatibility mode turns off extensions and most 
        MultiMarkdown-specific features */
    if (opt_compatibility)
        extensions = 0x000000;
    if (opt_compatibility)
        extensions = extensions | EXT_COMPATIBILITY;

    if (opt_to == NULL)
        output_format = HTML_FORMAT;
    else if (strcmp(opt_to, "html") == 0)
        output_format = HTML_FORMAT;
    else if (strcmp(opt_to, "latex") == 0)
        output_format = LATEX_FORMAT;
    else if (strcmp(opt_to, "memoir") == 0)
        output_format = MEMOIR_FORMAT;
    else if (strcmp(opt_to, "beamer") == 0)
        output_format = BEAMER_FORMAT;
    else if (strcmp(opt_to, "opml") == 0)
        output_format = OPML_FORMAT;
    else if (strcmp(opt_to, "odf") == 0)
        output_format = ODF_FORMAT;
    else {
        fprintf(stderr, "%s: Unknown output format '%s'\n", progname, opt_to);
        exit(EXIT_FAILURE);
    }

    numargs = argc - 1;

    if (opt_batchmode && numargs != 0) {
        /* handle each file individually, and set output to filename with
            appropriate extension */
        
           for (i = 0; i < numargs; i++) {
                inputbuf = g_string_new("");   /* string for concatenated input */
                /* Read file */
                if ((input = fopen(argv[i+1], "r")) == NULL) {
                    perror(argv[i+1]);
                    exit(EXIT_FAILURE);
                }
                while ((curchar = fgetc(input)) != EOF)
                    g_string_append_c(inputbuf, curchar);
                fclose(input);

                /* Display metadata on request */
                if (opt_extract_meta) {
                    out = extract_metadata_value(inputbuf->str, extensions, opt_extract_meta);
                    if (out != NULL) fprintf(stdout, "%s\n", out);
                    return(EXIT_SUCCESS);
                }
                
                /* remove file extension, if present */
                fake = argv[i+1];
                if (strrchr(fake, '.') != NULL) {
                    int count = strrchr(fake,'.') - fake;
                    if (count != 0) {
                        fake[count] = '\0';
                    }
                }

                file = g_string_new(fake);
                if (output_format == HTML_FORMAT) {
                    g_string_append(file,".html");
                } else if (output_format == OPML_FORMAT) {
                    g_string_append(file,".opml");
                } else if (output_format == ODF_FORMAT) {
                    g_string_append(file,".fodt");
                } else {
                    g_string_append(file,".tex");
                }

                /* open output file */
                if (!(output = fopen(file->str, "w"))) {
                    perror(opt_output);
                    return 1;
                }
               
                out = markdown_to_string(inputbuf->str, extensions, output_format);

                fprintf(output, "%s\n", out);
                fclose(output);
                g_string_free(file,true);
                free(out);
                g_string_free(inputbuf, true);
           }
        
    } else {
        /* Read input from stdin or input files into inputbuf */

        inputbuf = g_string_new("");   /* string for concatenated input */

        if (numargs == 0) {        /* use stdin if no files specified */
            while ((curchar = fgetc(stdin)) != EOF)
                g_string_append_c(inputbuf, curchar);
            fclose(stdin);
        }
        else {                  /* open all the files on command line */
           for (i = 0; i < numargs; i++) {
                if ((input = fopen(argv[i+1], "r")) == NULL) {
                    perror(argv[i+1]);
                    exit(EXIT_FAILURE);
                }
                while ((curchar = fgetc(input)) != EOF)
                    g_string_append_c(inputbuf, curchar);
                fclose(input);
           }
        }

        /* Display metadata on request */
        if (opt_extract_meta) {
            out = extract_metadata_value(inputbuf->str, extensions, opt_extract_meta);
            if (out != NULL) fprintf(stdout, "%s\n", out);
            return(EXIT_SUCCESS);
        }
        
       /* we allow "-" as a synonym for stdout here */
        if (opt_output == NULL || strcmp(opt_output, "-") == 0)
            output = stdout;
        else if (!(output = fopen(opt_output, "w"))) {
            perror(opt_output);
            return 1;
        }

        out = markdown_to_string(inputbuf->str, extensions, output_format);
        fprintf(output, "%s\n", out);
        free(out);
        fclose(output);
        g_string_free(inputbuf, true);
        
    }

#if STANDALONE_MAC_VERSION
	[toolPool release];
#endif

    return(EXIT_SUCCESS);
}
