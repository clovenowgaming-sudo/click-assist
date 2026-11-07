#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <algorithm>
#include <deque>
#include <fstream>
#include <vector>

using namespace geode::prelude;

static double g_now = 0.0;
static double g_clickAt = -100.0;
static double g_clickX = 0.0;
static double g_prevX = 0.0;
static double g_speed = 0.0;
static double g_lastReport = -100.0;
static double g_lastSave = -100.0;

static bool g_searching = false;
static bool g_bypass = false;
static bool g_simDead = false;
static bool g_simWon = false;
static int g_ticks = 0;

static char const* frameKey(PlayerObject* p) {
	if (p->m_isShip) return "ship-frames";
	if (p->m_isBird) return "ufo-frames";
	if (p->m_isBall) return "ball-frames";
	if (p->m_isDart) return "wave-frames";
	if (p->m_isRobot) return "robot-frames";
	if (p->m_isSpider) return "spider-frames";
	if (p->m_isSwing) return "swing-frames";
	return "cube-frames";
}

static int bufferFrames(PlayerObject* p) {
	if (!p) return 0;
	return static_cast<int>(Mod::get()->getSettingValue<int64_t>(frameKey(p)));
}

static double frameLen() {
	auto fps = Mod::get()->getSettingValue<int64_t>("frame-rate");
	if (fps < 1) fps = 1;
	return 1.0 / static_cast<double>(fps);
}

static std::string fmtFrames(double sec) {
	return fmt::format("{:.1f} frames ({:.0f} ms)", sec / frameLen(), sec * 1000.0);
}

static void note(std::string const& text) {
	log::info("[ClickAssist] {}", text);
	if (!Mod::get()->getSettingValue<bool>("notify")) return;
	Notification::create(text, NotificationIcon::None, 0.9f)->show();
}

static CCRect shrinkRect(CCRect r, float scale) {
	float w = r.size.width * scale;
	float h = r.size.height * scale;
	return CCRect(r.getMidX() - w / 2.f, r.getMidY() - h / 2.f, w, h);
}

static bool rectsOverlap(CCRect const& a, CCRect const& b) {
	return a.getMinX() <= b.getMaxX() && a.getMaxX() >= b.getMinX()
		&& a.getMinY() <= b.getMaxY() && a.getMaxY() >= b.getMinY();
}

static void drawBox(CCDrawNode* n, CCRect const& r, ccColor4F color) {
	n->drawRect(
		CCPoint(r.getMinX(), r.getMinY()),
		CCPoint(r.getMaxX(), r.getMaxY()),
		ccColor4F{0.f, 0.f, 0.f, 0.f}, 0.6f, color
	);
}

struct PadState {
	bool userDown = false;
	bool gameDown = false;
	bool pending = false;
	bool armed = false;
	bool fell = false;
	bool wasGround = true;
	double pressAt = 0.0;
	double releaseAt = 0.0;
	double leftAt = 0.0;
};

class $modify(CAGameLayer, GJBaseGameLayer) {
	struct Fields {
		double now = 0.0;
		PadState pads[2];
	};

	bool active() {
		if (!Mod::get()->getSettingValue<bool>("enabled")) return false;
		return typeinfo_cast<PlayLayer*>(this) != nullptr;
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		if (g_searching || g_bypass || button != 1 || !this->active()) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}
		auto player = isPlayer1 ? m_player1 : m_player2;
		if (!player) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		auto f = m_fields.self();
		auto& s = f->pads[isPlayer1 ? 0 : 1];

		if (down) {
			if (isPlayer1) {
				g_clickAt = f->now;
				g_clickX = player->getPositionX();
			}
			s.userDown = true;
			s.pending = false;

			if (!player->m_isOnGround && s.fell && (f->now - s.leftAt) <= 0.25) {
				note("Late click: " + fmtFrames(f->now - s.leftAt));
				s.fell = false;
			}

			s.armed = !player->m_isOnGround;
			s.pressAt = f->now;

			if (s.gameDown) {
				GJBaseGameLayer::handleButton(false, 1, isPlayer1);
			}
			s.gameDown = true;
			GJBaseGameLayer::handleButton(true, 1, isPlayer1);
			return;
		}

		s.userDown = false;
		int frames = bufferFrames(player);
		if (frames > 0 && s.gameDown) {
			double hold = frames * frameLen();
			double elapsed = f->now - s.pressAt;
			if (elapsed < hold) {
				s.pending = true;
				s.releaseAt = s.pressAt + hold;
				return;
			}
		}
		s.pending = false;
		s.gameDown = false;
		GJBaseGameLayer::handleButton(false, 1, isPlayer1);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
		if (g_searching) return;
		g_ticks++;
		if (!this->active()) return;

		auto f = m_fields.self();
		f->now += dt;
		g_now = f->now;

		if (m_player1 && dt > 0.f) {
			double x = m_player1->getPositionX();
			if (x > g_prevX) g_speed = (x - g_prevX) / dt;
			g_prevX = x;
		}

		for (int i = 0; i < 2; i++) {
			auto p = (i == 0) ? m_player1 : m_player2;
			if (!p) continue;
			bool isP1 = (i == 0);
			auto& s = f->pads[i];
			bool g = p->m_isOnGround;

			if (s.pending && f->now >= s.releaseAt) {
				s.pending = false;
				s.gameDown = false;
				GJBaseGameLayer::handleButton(false, 1, isP1);
			}

			if (g && !s.wasGround) {
				s.fell = false;
				if (s.armed && s.gameDown) {
					double early = f->now - s.pressAt;
					if (early <= 0.5) {
						note("Early click: " + fmtFrames(early) + (s.userDown ? " (held)" : " (buffered)"));
					}
					if (p->m_isBall || p->m_isSpider) {
						GJBaseGameLayer::handleButton(false, 1, isP1);
						GJBaseGameLayer::handleButton(true, 1, isP1);
						s.pressAt = f->now;
						if (s.pending) {
							s.releaseAt = std::max(s.releaseAt, f->now + 2.0 * frameLen());
						}
					}
				}
				s.armed = false;
			} else if (!g && s.wasGround) {
				s.leftAt = f->now;
				s.fell = !s.gameDown;
			}

			s.wasGround = g;
		}
	}
};

class $modify(CAPlayLayer, PlayLayer) {
	struct Fields {
		CCDrawNode* node = nullptr;
		std::deque<CCRect> trail;
		std::vector<std::pair<int, bool>> record;
		std::vector<std::pair<int, bool>> replay;
		size_t replayIdx = 0;
		bool wantDown = false;
		int flipLeft = 0;
		bool flipBack = false;
		int sinceDecide = 999;
	};

	std::filesystem::path pathFile() {
		int id = static_cast<int>(m_level->m_levelID.value());
		return Mod::get()->getSaveDir() / fmt::format("path_{}.txt", id);
	}

	void loadReplay() {
		auto f = m_fields.self();
		f->replay.clear();
		f->replayIdx = 0;
		if (!Mod::get()->getSettingValue<bool>("replay-path")) return;
		std::ifstream in(this->pathFile());
		int t, d;
		while (in >> t >> d) f->replay.push_back({t, d != 0});
		if (f->replay.empty()) note("No saved path for this level");
		else note(fmt::format("Loaded path: {} inputs", f->replay.size()));
	}

	void saveRecord() {
		auto f = m_fields.self();
		if (f->record.empty()) return;
		std::ofstream out(this->pathFile());
		for (auto& e : f->record) out << e.first << " " << (e.second ? 1 : 0) << "\n";
		note(fmt::format("Path saved: {} inputs", f->record.size()));
	}

	void setHeld(bool b) {
		auto f = m_fields.self();
		if (b == f->wantDown) return;
		f->wantDown = b;
		g_bypass = true;
		this->handleButton(b, 1, true);
		g_bypass = false;
		f->record.push_back({g_ticks, b});
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		if (g_searching) return;
		auto f = m_fields.self();
		g_ticks = 0;
		f->record.clear();
		f->wantDown = false;
		f->flipLeft = 0;
		f->sinceDecide = 999;
		f->trail.clear();
		this->loadReplay();
	}

	void levelComplete() {
		if (g_searching) {
			g_simWon = true;
			return;
		}
		if (Mod::get()->getSettingValue<bool>("enabled")
			&& Mod::get()->getSettingValue<bool>("pathfinder-enabled")) {
			this->saveRecord();
		}
		PlayLayer::levelComplete();
	}

	void pfDecide(float dt) {
		auto f = m_fields.self();
		int horizon = static_cast<int>(Mod::get()->getSettingValue<int64_t>("pf-horizon"));
		bool cur = f->wantDown;

		auto cp = this->createCheckpoint();
		if (!cp) return;
		cp->retain();

		int const flips[4] = {0, 100000, 1, 3};
		int bestIdx = 0;
		long long bestScore = -1;
		bool foundFull = false;

		for (int c = 0; c < 4; c++) {
			if (c > 0) this->loadFromCheckpoint(cp);
			g_searching = true;
			g_simDead = false;
			g_simWon = false;
			bool simState = cur;
			int survived = 0;
			for (int s = 0; s < horizon; s++) {
				bool want = (s < flips[c]) ? !cur : cur;
				if (want != simState) {
					this->handleButton(want, 1, true);
					simState = want;
				}
				PlayLayer::update(dt);
				if (g_simDead) break;
				survived++;
				if (g_simWon) {
					survived = horizon;
					break;
				}
			}
			g_searching = false;
			long long score = static_cast<long long>(survived) * 1000000LL
				+ static_cast<long long>(m_player1 ? m_player1->getPositionX() : 0.f);
			if (survived >= horizon && !foundFull) {
				foundFull = true;
				bestIdx = c;
				bestScore = score;
				break;
			}
			if (score > bestScore) {
				bestScore = score;
				bestIdx = c;
			}
		}

		this->loadFromCheckpoint(cp);
		cp->release();

		if (flips[bestIdx] > 0) {
			this->setHeld(!cur);
			if (flips[bestIdx] < 100000) {
				f->flipLeft = flips[bestIdx];
				f->flipBack = cur;
			}
		}
	}

	void update(float dt) {
		if (g_searching || !Mod::get()->getSettingValue<bool>("enabled")) {
			PlayLayer::update(dt);
			return;
		}
		auto f = m_fields.self();
		bool alive = m_player1 && !m_player1->m_isDead;

		if (alive && !f->replay.empty() && Mod::get()->getSettingValue<bool>("replay-path")) {
			while (f->replayIdx < f->replay.size() && f->replay[f->replayIdx].first <= g_ticks) {
				g_bypass = true;
				this->handleButton(f->replay[f->replayIdx].second, 1, true);
				g_bypass = false;
				f->replayIdx++;
			}
		} else if (alive && Mod::get()->getSettingValue<bool>("pathfinder-enabled")) {
			if (f->flipLeft > 0) {
				if (--f->flipLeft == 0) this->setHeld(f->flipBack);
			} else {
				int interval = static_cast<int>(Mod::get()->getSettingValue<int64_t>("pf-interval"));
				if (++f->sinceDecide >= interval) {
					f->sinceDecide = 0;
					this->pfDecide(dt);
				}
			}
		}

		PlayLayer::update(dt);
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (g_searching) return;

		auto mod = Mod::get();
		auto f = m_fields.self();
		if (!f->node) {
			f->node = CCDrawNode::create();
			m_objectLayer->addChild(f->node, 1000);
		}
		f->node->clear();
		if (!mod->getSettingValue<bool>("enabled") || !m_player1) return;

		bool show = mod->getSettingValue<bool>("show-hitboxes");
		bool trail = mod->getSettingValue<bool>("hitbox-trail");

		if (show) {
			float px = m_player1->getPositionX();
			float range = static_cast<float>(mod->getSettingValue<int64_t>("hitbox-range"));
			for (auto obj : CCArrayExt<GameObject*>(m_objects)) {
				if (!obj) continue;
				if (std::fabs(obj->getPositionX() - px) > range) continue;
				auto t = obj->m_objectType;
				if (t == GameObjectType::Hazard) {
					drawBox(f->node, obj->getObjectRect(), ccColor4F{1.f, 0.1f, 0.1f, 1.f});
				} else if (t == GameObjectType::Solid) {
					drawBox(f->node, obj->getObjectRect(), ccColor4F{0.2f, 0.4f, 1.f, 1.f});
				}
			}
			drawBox(f->node, m_player1->getObjectRect(), ccColor4F{0.1f, 1.f, 0.2f, 1.f});
			if (m_player2) {
				drawBox(f->node, m_player2->getObjectRect(), ccColor4F{0.1f, 1.f, 0.2f, 1.f});
			}
		}

		if (trail) {
			size_t maxLen = static_cast<size_t>(mod->getSettingValue<int64_t>("trail-length"));
			f->trail.push_back(m_player1->getObjectRect());
			while (f->trail.size() > maxLen) f->trail.pop_front();
			size_t n = f->trail.size();
			for (size_t i = 0; i < n; i++) {
				float a = 0.1f + 0.6f * static_cast<float>(i) / static_cast<float>(n);
				drawBox(f->node, f->trail[i], ccColor4F{1.f, 1.f, 0.2f, a});
			}
		} else if (!f->trail.empty()) {
			f->trail.clear();
		}
	}

	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		auto mod = Mod::get();
		if (mod->getSettingValue<bool>("enabled")
			&& mod->getSettingValue<bool>("hitbox-enabled")
			&& player && obj && !player->m_isDead) {

			bool isHazard = obj->m_objectType == GameObjectType::Hazard;
			bool isSolid = obj->m_objectType == GameObjectType::Solid;
			bool blocks = mod->getSettingValue<bool>("shrink-blocks");

			if (isHazard || (isSolid && blocks)) {
				float hs = static_cast<float>(mod->getSettingValue<int64_t>("hazard-scale")) / 100.f;
				float ps = static_cast<float>(mod->getSettingValue<int64_t>("player-scale")) / 100.f;
				CCRect pr = shrinkRect(player->getObjectRect(), ps);
				CCRect orr = shrinkRect(obj->getObjectRect(), hs);
				if (!rectsOverlap(pr, orr)) {
					if (!g_searching && g_now - g_lastSave > 0.4) {
						g_lastSave = g_now;
						note("Hitbox saved you");
					}
					return;
				}
			}
		}

		if (g_searching) {
			g_simDead = true;
			return;
		}

		PlayLayer::destroyPlayer(player, obj);

		if (!mod->getSettingValue<bool>("enabled")) return;
		if (player != m_player1) return;
		if (g_now - g_lastReport < 0.5) return;
		g_lastReport = g_now;

		double since = g_now - g_clickAt;
		std::string msg;
		if (since > 2.0 || g_clickAt < 0.0) {
			msg = "Died: no click in the last 2 seconds";
		} else {
			msg = "Died " + fmtFrames(since) + " after your click";
			if (obj && g_speed > 1.0) {
				double dx = obj->getPositionX() - g_clickX;
				double framesAhead = dx / g_speed / frameLen();
				if (framesAhead >= 0.0) {
					msg += fmt::format(" | Hazard was {:.1f} frames ahead when you clicked", framesAhead);
				} else {
					msg += fmt::format(" | You clicked {:.1f} frames AFTER the hazard", -framesAhead);
				}
			}
		}
		note(msg);
	}
};

class $modify(CAPause, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto win = CCDirector::get()->getWinSize();
		auto menu = CCMenu::create();
		menu->setPosition({0.f, 0.f});

		auto gear = CCSprite::createWithSpriteFrameName("GJ_optionsBtn02_001.png");
		gear->setScale(0.8f);
		auto settingsBtn = CCMenuItemSpriteExtra::create(gear, this, menu_selector(CAPause::onCASettings));
		settingsBtn->setPosition({win.width - 30.f, 30.f});
		menu->addChild(settingsBtn);

		auto pathSpr = ButtonSprite::create("Path");
		pathSpr->setScale(0.7f);
		auto pathBtn = CCMenuItemSpriteExtra::create(pathSpr, this, menu_selector(CAPause::onCAPath));
		pathBtn->setPosition({win.width - 90.f, 30.f});
		menu->addChild(pathBtn);

		this->addChild(menu, 100);
	}

	void onCASettings(CCObject*) {
		openSettingsPopup(Mod::get());
	}

	void onCAPath(CCObject*) {
		bool now = !Mod::get()->getSettingValue<bool>("pathfinder-enabled");
		Mod::get()->setSettingValue<bool>("pathfinder-enabled", now);
		Notification::create(now ? "Pathfinder ON" : "Pathfinder OFF", NotificationIcon::None, 1.0f)->show();
	}
};
