/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_SAVEGAME_RENAME_H
#define DATAOBJ_SAVEGAME_RENAME_H


class karte_t;

/**
 * Fork: renames objects of a loaded game from a list, for the command line switch -rename
 * (then -saveas writes the result). One object per line, fields separated by tabs:
 *
 *   KIND  ID  OLD NAME  NEW NAME
 *
 * KIND and ID: halt HALT_ID, line LINE_ID, convoy CONVOY_ID, player PLAYER_NR,
 * city X,Y (its position as in the JSON export), factory X,Y,Z and marker X,Y,Z (tile).
 * OLD NAME must be the current name (a convoy may give it with or without the id shown in
 * front), or * to skip that check; a row whose name does not match is left alone.
 * A stop may not get the name of another stop (loading would rename one of them). Rows are
 * applied in order, so to swap two names go through a third one.
 * Empty lines and lines starting with # are ignored. The file is UTF-8.
 * The renames go through the rename tool, as in the game. A report with one line per row
 * (status, kind, id, name before, name after) is written to report_path.
 */
class savegame_rename_t
{
public:
	/// @return number of rows that could not be applied (0 = all done)
	static int apply( karte_t *welt, const char *list_path, const char *report_path );
};

#endif
