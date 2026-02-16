using InferenceClientUI.Models.Entities;
using Microsoft.Data.Sqlite;

namespace InferenceClientUI.Services.Database
{
    /// <summary>
    /// global_setting 테이블 CRUD.
    /// C++ GlobalSettingSqliteRepository 역할.
    /// </summary>
    public sealed class GlobalSettingRepository
    {
        private readonly SqliteConnection _db;

        public GlobalSettingRepository(SqliteConnection db) => _db = db;

        public bool Save(GlobalSettingEntity e)
        {
            using var cmd = _db.CreateCommand();
            cmd.CommandText = """
                INSERT INTO global_setting (log_dir, auto_start, language, disk_threshold)
                VALUES ($logDir, $autoStart, $lang, $disk);
                """;
            BindParams(cmd, e);
            return cmd.ExecuteNonQuery() > 0;
        }

        public bool Update(GlobalSettingEntity e)
        {
            using var cmd = _db.CreateCommand();
            cmd.CommandText = """
                UPDATE global_setting SET
                    log_dir=$logDir, auto_start=$autoStart,
                    language=$lang, disk_threshold=$disk
                WHERE id=$id;
                """;
            cmd.Parameters.AddWithValue("$id", e.Id);
            BindParams(cmd, e);
            return cmd.ExecuteNonQuery() > 0;
        }

        public GlobalSettingEntity? FindOne()
        {
            using var cmd = _db.CreateCommand();
            cmd.CommandText = """
                SELECT id, log_dir, auto_start, language, disk_threshold
                FROM global_setting
                ORDER BY id DESC
                LIMIT 1;
                """;
            using var r = cmd.ExecuteReader();
            if (!r.Read()) return null;

            return new GlobalSettingEntity
            {
                Id = r.GetInt64(r.GetOrdinal("id")),
                LogDir = r.GetString(r.GetOrdinal("log_dir")),
                AutoStart = r.GetInt32(r.GetOrdinal("auto_start")) != 0,
                Language = r.GetInt32(r.GetOrdinal("language")),
                DiskThreshold = r.GetInt32(r.GetOrdinal("disk_threshold")),
            };
        }

        private static void BindParams(SqliteCommand cmd, GlobalSettingEntity e)
        {
            cmd.Parameters.AddWithValue("$logDir", e.LogDir);
            cmd.Parameters.AddWithValue("$autoStart", e.AutoStart ? 1 : 0);
            cmd.Parameters.AddWithValue("$lang", e.Language);
            cmd.Parameters.AddWithValue("$disk", e.DiskThreshold);
        }
    }
}
