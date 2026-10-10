#ifndef FRONTIER_DEV_RENDER_H
#define FRONTIER_DEV_RENDER_H

/**
 * Developer tool: generates a world, saves it as the "frontier_world" scenario and starts it.
 * frontier_dev_render_update then saves a full city screenshot and a minimap screenshot with the game's own
 * graphics once the city has been drawn.
 * With "setup=1" it instead shows the frontier setup window and saves a screenshot of it; "setup=2" does the same
 * for the main menu.
 * @param spec Comma separated settings, e.g. "seed=42,template=coastal,size=160,climate=0,water=50,relief=50,forest=50"
 * @return 1 on success, 0 on failure
 */
int frontier_dev_render_start(const char *spec);

/**
 * Call once per frame, after drawing.
 * @return 0 while waiting, 1 when the screenshots are saved, -1 on failure
 */
int frontier_dev_render_update(void);

#endif // FRONTIER_DEV_RENDER_H
