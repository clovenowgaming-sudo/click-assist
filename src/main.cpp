#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

class $modify(ClickAssistPlayer, PlayerObject) {
	struct Fields {
		float buffer = 0.f;
		float sinceGround = 999.f;
		bool jumpedSinceGround = false;
		bool injecting = false;
		bool userHolding = false;
		int releaseDelay = 0;
	};

	bool active() {
		if (!Mod::get()->getSettingValue<bool>("enabled")) return false;
		auto pl = PlayLayer::get();
		if (!pl) return false;
		return this == pl->m_player1 || this == pl->m_player2;
	}

	bool simpleMode() {
		return !m_isShip && !m_isBird && !m_isBall && !m_isDart
			&& !m_isRobot && !m_isSpider && !m_isSwing;
	}

	bool pushButton(PlayerButton btn) {
		auto f = m_fields.self();
		if (f->injecting || btn != PlayerButton::Jump || !this->active()) {
			return PlayerObject::pushButton(btn);
		}
		f->userHolding = true;
		if (this->simpleMode()) {
			if (m_isOnGround) {
				f->jumpedSinceGround = true;
			} else {
				float coyote = static_cast<float>(Mod::get()->getSettingValue<double>("coyote-ms")) / 1000.f;
				if (!f->jumpedSinceGround && f->sinceGround <= coyote) {
					// Coyote time: baru jatuh dari tanah, masih dianggap grounded
					f->jumpedSinceGround = true;
					f->buffer = 0.f;
					m_isOnGround = true;
					bool r = PlayerObject::pushButton(btn);
					m_isOnGround = false;
					return r;
				}
				// Input buffer: ingat klik, jump saat mendarat
				f->buffer = static_cast<float>(Mod::get()->getSettingValue<double>("buffer-ms")) / 1000.f;
			}
		}
		return PlayerObject::pushButton(btn);
	}

	bool releaseButton(PlayerButton btn) {
		if (btn == PlayerButton::Jump && !m_fields->injecting) {
			m_fields->userHolding = false;
		}
		return PlayerObject::releaseButton(btn);
	}

	void update(float dt) {
		PlayerObject::update(dt);
		if (!this->active()) return;
		auto f = m_fields.self();

		if (f->releaseDelay > 0 && --f->releaseDelay == 0 && !f->userHolding) {
			f->injecting = true;
			PlayerObject::releaseButton(PlayerButton::Jump);
			f->injecting = false;
		}

		if (!this->simpleMode()) {
			f->buffer = 0.f;
			f->jumpedSinceGround = false;
			f->sinceGround = 0.f;
			return;
		}

		if (m_isOnGround) {
			f->sinceGround = 0.f;
			f->jumpedSinceGround = false;
			if (f->buffer > 0.f) {
				f->buffer = 0.f;
				if (!f->userHolding) {
					f->injecting = true;
					PlayerObject::pushButton(PlayerButton::Jump);
					f->injecting = false;
					f->jumpedSinceGround = true;
					f->releaseDelay = 2;
				}
			}
		} else {
			f->sinceGround += dt;
			f->buffer = std::max(0.f, f->buffer - dt);
		}
	}
};
