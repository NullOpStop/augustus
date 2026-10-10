#ifndef FRONTIER_DEV_RENDER_H
#define FRONTIER_DEV_RENDER_H

/**
 * Developer tool: generates a world, saves it as the "frontier_preview" scenario and starts it.
 * frontier_dev_render_update then saves a full city screenshot and a minimap screenshot with the game's own
 * graphics once the city has been drawn.
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
