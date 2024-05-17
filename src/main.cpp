#include <Geode/Geode.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/utils/web.hpp>

#include <ghc/filesystem.hpp>

#include <string>
#include <functional>

#include "funcs.hpp"

using namespace geode::prelude;
namespace fs = ghc::filesystem;

bool dailySaved = false;
bool weeklySaved = false;

// This was used to pre-save most data to be loaded into the mod's save data so the user doesn't have to download it all from the servers. It isn't used in the mod.
void saveFile(int level) {
    auto file = Mod::get()->getSaveDir() / "data.txt";
    std::ofstream out(file, std::ios::app);
    if (out.is_open()) {
		out << level << ",";
        out.close();
    }
}

// Load the pre-saved data into the mod save data
void loadFile() {
	// log::debug("Loading file...");
	auto file = Mod::get()->getResourcesDir() / "data.txt";
	std::ifstream in(file);
	if (in.is_open()) {
		std::string line;
		while (std::getline(in, line)) {
			std::vector<std::string> levels = splitString(line, ',');
			for (std::string level : levels) {
				if (level.empty()) {
					continue;
				}
				try {
					int id = std::stoi(level);
					Mod::get()->setSavedValue<bool>(std::to_string(id), true);
				} catch (const std::invalid_argument& e) {
					// log::debug("Failed to convert to int: {}", level);
				}
			}
		}
		in.close();
	}
	Mod::get()->setSavedValue<bool>("loaded", true);
}

// Gets a page of daily/weekly levels
void saveLevels(int pagesChecked, bool weekly, std::function<void(bool)> finalCallback) {
	// Resume from the page that it was left off on
	int page = weekly ? Mod::get()->getSavedValue<int>("weeklyPage") : Mod::get()->getSavedValue<int>("dailyPage");
	int maxPages = 3;
	int type = weekly ? 22 : 21;
    // log::debug("Grabbing page {} with type {} (21=d, 22=w)", page + 1, type);

	// Request the page from the servers
    web::AsyncWebRequest()
        .userAgent("")
        .postRequest()
        .bodyRaw(fmt::format("diff=-&type={}&page={}&len=-&secret=Wmfd2893gb7", type, page))
        .fetch("http://www.boomlings.com/database/getGJLevels21.php")
        .text()
        .then([page, pagesChecked, maxPages, weekly, finalCallback](std::string const& result) mutable {
			// If the request returns -1, there are no more pages
			if (result == "-1") {
                // log::debug("Request returned -1, no more pages");
                finalCallback(false);
                return;
            }

            std::vector<int> levelIDs = parseData(result);
            bool allNew = true;

			int dupes = 0;
			// Go through each level on this page and save it
            for (int level : levelIDs) {
				// Check for levels that already exist in the save
                if (Mod::get()->getSavedValue<bool>(std::to_string(level))) {
					dupes++;
					// log::debug("{} already saved. Dupes: {}", level, dupes);		
                }
				// If there are 3 dupes, break since the data is already saved
				if (dupes >= 3) {
					// log::debug("Too many dupes on this page (meaning the data is already saved, not an accidental dupe on the servers), breaking and resetting page...");
                    allNew = false;
                    break;
				}
				// Save level
                Mod::get()->setSavedValue<bool>(std::to_string(level), true);
                // log::debug("Saved {}", level);
            }
			
			
			pagesChecked++;
			// All (technically most) levels on this page are new, continue to the next page
            if (allNew && pagesChecked < maxPages) {
				// log::debug("pages checked: {}", pagesChecked);
				weekly ? Mod::get()->setSavedValue<int>("weeklyPage", page + 1) : Mod::get()->setSavedValue<int>("dailyPage", page + 1);
                saveLevels(pagesChecked + 1, weekly, finalCallback);
            }
			// Max pages checked, stop
			else if (pagesChecked >= maxPages) {
				// log::debug("Max pages checked ({} >= {}), storing next page number in settings and stopping", pagesChecked, maxPages);
				weekly ? Mod::get()->setSavedValue<int>("weeklyPage", page + 1) : Mod::get()->setSavedValue<int>("dailyPage", page + 1);
				finalCallback(allNew);
			} 
			// Reached the end of pages/found existing data, stop
			else {
                // log::debug("Finished processing pages");
				weekly ? Mod::get()->setSavedValue<int>("weeklyPage", 0) : Mod::get()->setSavedValue<int>("dailyPage", 0);
				weekly ? weeklySaved = true : dailySaved = true;
                finalCallback(allNew);
            }
        })
		// Error
        .expect([finalCallback](std::string const& error) {
            // log::debug("error: {}", error);
            finalCallback(false);
        });
}

// Returns true if a level has been daily/weekly
bool wasDaily(int level) {
	// log::debug("ID: {}, Daily/Weekly: {}", level, Mod::get()->getSavedValue<bool>(std::to_string(level)));
	return Mod::get()->getSavedValue<bool>(std::to_string(level));
}

// Saves daily/weekly data (either initially from the pre-saved data or from the servers)
// This is purposefully done in LevelBrowserLayer as a sort of "buffer" to prevent rate limits (if a lot of data needs to be requested)
class $modify(LevelBrowserLayer) {
    bool init(GJSearchObject* p0) {
        bool result = LevelBrowserLayer::init(p0);

		// Load pre-saved data (first time mod is loaded)
		if (Mod::get()->getSavedValue<bool>("loaded") == false) {
			loadFile();
		}

		// Load new daily data
        if (dailySaved == false) {
			// Load new data
            saveLevels(0, false, [this](bool allNew){});
        } else {
            // log::debug("Daily levels have already been saved this session");
        }

		// Load new weekly data
		if (weeklySaved == false) {
            saveLevels(0, true, [this](bool allNew){});
        } else {
            // log::debug("Weekly levels have already been saved this session");
        }

        return result;
    }
};

// Add indicator to level cell
class $modify(LevelCell) {
	CCSprite* indicator;
	bool isList = false;

	void loadCustomLevelCell() {
		LevelCell::loadCustomLevelCell();

		// Only show if the setting is enabled
		if (!Mod::get()->getSettingValue<bool>("levelcell")) return;

		// Check if the level being loaded is the current daily/weekly (since they don't show in the safe)
		if (m_level->m_dailyID.value() != 0) {
			// log::debug("Daily/Weekly detected, saving current level");
			Mod::get()->setSavedValue<bool>(std::to_string(m_level->m_levelID.value()), true);
		}

		// Only create an indicator this if the level has been daily/weekly
		if (wasDaily(m_level->m_levelID.value())) {
			CCLayer* mainLayer = static_cast<CCLayer*>(getChildByID("main-layer"));

			// Goofy hack to check if a level is being shown in a list/compact list
			CCSprite* likeSprite = static_cast<CCSprite*>(mainLayer->getChildByID("likes-icon"));
			if (likeSprite->getScale() < 0.6) {
				m_fields->isList = true;
			}

			// Daily
			m_fields->indicator = CCSprite::create("daily.png"_spr);
			// Weekly
			if (m_level->m_demon.value() != 0) {
				m_fields->indicator = CCSprite::create("weekly.png"_spr);
			}
			
			// Scale
			m_fields->indicator->setScale(0.5);
			if (m_fields->isList) m_fields->indicator->setScale(0.4);
			
			// Find the position to place the indicator
			// Both indicators
			if (static_cast<CCSprite*>(mainLayer->getChildByID("copy-indicator")) && static_cast<CCSprite*>(mainLayer->getChildByID("high-object-indicator"))) {
				// Use object indicator as the anchor (always on the right)
				CCSprite* anchor = static_cast<CCSprite*>(mainLayer->getChildByID("high-object-indicator"));
				// Set position
				m_fields->indicator->setPosition(anchor->convertToWorldSpace(getPosition()));
				if (!m_fields->isList) {
					m_fields->indicator->setPosition({m_fields->indicator->getPositionX() + 22, m_fields->indicator->getPositionY() + 6});
				} else {
					m_fields->indicator->setPosition({m_fields->indicator->getPositionX() + 19, m_fields->indicator->getPositionY() + 4.7f});
				}
			}
			// One indicator
			else if (static_cast<CCSprite*>(mainLayer->getChildByID("copy-indicator")) || static_cast<CCSprite*>(mainLayer->getChildByID("high-object-indicator"))) {
				// Use whichever indicator is present as the anchor
				CCSprite* anchor = static_cast<CCSprite*>(mainLayer->getChildByID("copy-indicator") ? mainLayer->getChildByID("copy-indicator") : mainLayer->getChildByID("high-object-indicator"));
				// Set position
				m_fields->indicator->setPosition(anchor->convertToWorldSpace(getPosition()));
				if (!m_fields->isList) {
					m_fields->indicator->setPosition({m_fields->indicator->getPositionX() + 22, m_fields->indicator->getPositionY() + 6});
				} else {
					m_fields->indicator->setPosition({m_fields->indicator->getPositionX() + 19, m_fields->indicator->getPositionY() + 4.7f});
				}
			}
			// No indicators
			else {
				// Use last character of the creator name as anchor
				CCLayer* mainMenu = static_cast<CCLayer*>(mainLayer->getChildByID("main-menu"));
				CCMenuItemSpriteExtra* name = static_cast<CCMenuItemSpriteExtra*>(mainMenu->getChildByID("creator-name"));
				CCLabelBMFont* nameText = static_cast<CCLabelBMFont*>(name->getChildren()->objectAtIndex(0));
				std::string nameStr = nameText->getString();
				// If in a list, the name can possibly be empty if it isn't finished loading, so just ignore for now (it will seemingly run the function again when done)
				if (nameStr.length() != 0) {
					CCSprite* anchor = static_cast<CCSprite*>(nameText->getChildren()->objectAtIndex(nameText->getChildren()->count() - 1));
					// Set position
					m_fields->indicator->setPosition(anchor->convertToWorldSpace(getPosition()));
					if (!m_fields->isList) {
						m_fields->indicator->setPosition({m_fields->indicator->getPositionX() + 25, m_fields->indicator->getPositionY() + 7});
					} else {
						m_fields->indicator->setPosition({m_fields->indicator->getPositionX() + 20, m_fields->indicator->getPositionY() + 5.7f});
					}
				}
			}

			// Add indicator
			mainLayer->addChild(m_fields->indicator);	
		}
	}
};

// Add indicator to level info
class $modify(LevelInfoLayer) {
	CCSprite* indicator;

	bool init(GJGameLevel* p0, bool p1) {
		bool result = LevelInfoLayer::init(p0, p1);

		// Only show if the setting is enabled
		if (!Mod::get()->getSettingValue<bool>("levelinfo")) return result;

		// Only create an indicator this if the level has been daily/weekly
		if (wasDaily(m_level->m_levelID.value())) {
			// Daily
			m_fields->indicator = CCSprite::create("daily.png"_spr);
			// Weekly
			if (m_level->m_demon.value() != 0) {
				m_fields->indicator = CCSprite::create("weekly.png"_spr);
			}
				
			// Scale
			m_fields->indicator->setScale(0.5);
			
			// Find the position to place the indicator
			// Both indicators
			if (static_cast<CCSprite*>(getChildByID("copy-indicator")) && static_cast<CCSprite*>(getChildByID("high-object-indicator"))) {
				// Use object indicator as the anchor (always on the right)
				CCSprite* anchor = static_cast<CCSprite*>(getChildByID("high-object-indicator"));
				// Set position
				m_fields->indicator->setPosition(anchor->convertToWorldSpace(getPosition()));
				m_fields->indicator->setPosition({m_fields->indicator->getPositionX() + 23, m_fields->indicator->getPositionY() + 6});
			}
			// One indicator
			else if (static_cast<CCSprite*>(getChildByID("copy-indicator")) || static_cast<CCSprite*>(getChildByID("high-object-indicator"))) {
				// Use whichever indicator is present as the anchor
				CCSprite* anchor = static_cast<CCSprite*>(getChildByID("copy-indicator") ? getChildByID("copy-indicator") : getChildByID("high-object-indicator"));
				// Set position
				m_fields->indicator->setPosition(anchor->convertToWorldSpace(getPosition()));
				m_fields->indicator->setPosition({m_fields->indicator->getPositionX() + 23, m_fields->indicator->getPositionY() + 6});
			}
			// No indicators
			else {
				// Use last character of the creator name as anchor
				CCLayer* mainMenu = static_cast<CCLayer*>(getChildByID("creator-info-menu"));
				CCMenuItemSpriteExtra* name = static_cast<CCMenuItemSpriteExtra*>(mainMenu->getChildByID("creator-name"));
				CCLabelBMFont* nameText = static_cast<CCLabelBMFont*>(name->getChildren()->objectAtIndex(0));
				CCSprite* anchor = static_cast<CCSprite*>(nameText->getChildren()->objectAtIndex(nameText->getChildren()->count() - 1));
				// Set position
				m_fields->indicator->setPosition(anchor->convertToWorldSpace(getPosition()));
				m_fields->indicator->setPosition({m_fields->indicator->getPositionX() + 30, m_fields->indicator->getPositionY() + 9});
			}

			// Add indicator
			addChild(m_fields->indicator);
		}

		return result;
	}
};