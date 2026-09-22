#pragma once

// Progression data and derived stats are independent of any level or renderer.
class PlayerStats
{
  public:

    bool Dead() const;
    int Level() const;
    int Experience() const;
    int NextExperience() const;
    int WeaponRank() const;
    int Kills() const;
    int ChipsCollected() const;
    float Health() const;
    float MaxHealth() const;
    float Damage() const;
    float FireInterval() const;
    float Range() const;
    float MagnetRadius() const;
    float MovementSpeed() const;
    float Invulnerability() const;
    float ShotCooldown() const;
    bool GainExperience(int amount);
    int level_ = 1, experience_ = 0, weaponRank_ = 0, kills_ = 0, chipsCollected_ = 0;
    float health_ = 100, invulnerability_ = 1.5f, fireTimer_ = 0;
};
