/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef DATAOBJ_SAVEGAME_EXPORT_H
#define DATAOBJ_SAVEGAME_EXPORT_H


class karte_t;

/**
 * Dumps the state of a loaded game into a JSON file.
 * Used by the command line switch -export, which writes the file and quits.
 */
class savegame_export_t
{
public:
	/**
	 * Writes the whole game state as one JSON object to path.
	 * @param welt the loaded world
	 * @param save_name name of the savegame as given to -load (may be NULL)
	 * @param path absolute or relative file name of the JSON file to create
	 * @return true if the file was written completely
	 */
	static bool write_json( karte_t *welt, const char *save_name, const char *path );
};

#endif
