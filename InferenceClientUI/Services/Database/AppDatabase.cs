using Microsoft.Data.Sqlite;
using System;
using System.IO;

namespace InferenceClientUI.Services.Database
{
    /// <summary>
    /// SQLite 연결을 관리하고, 앱 시작 시 테이블을 생성하는 싱글톤.
    /// C++ Registry::getDbConnection() 역할.
    /// </summary>
    public sealed class AppDatabase : IDisposable
    {
        private readonly SqliteConnection _connection;

        public static AppDatabase Instance { get; } = new();

        public SqliteConnection Connection => _connection;

        private AppDatabase()
        {
            string dbPath = Path.Combine(AppContext.BaseDirectory, "ivs.db");
            _connection = new SqliteConnection($"Data Source={dbPath}");
            _connection.Open();

            using var pragma = _connection.CreateCommand();
            pragma.CommandText = "PRAGMA foreign_keys = ON;";
            pragma.ExecuteNonQuery();
        }

        /// <summary>앱 시작 시 호출하여 모든 테이블을 생성합니다.</summary>
        public void EnsureTables()
        {
            using var cmd = _connection.CreateCommand();
            cmd.CommandText = """
                CREATE TABLE IF NOT EXISTS client_setting (
                    id               INTEGER PRIMARY KEY AUTOINCREMENT,
                    name             TEXT    NOT NULL DEFAULT '',
                    cam_path         TEXT    NOT NULL DEFAULT '',
                    ip               TEXT    NOT NULL DEFAULT '0.0.0.0',
                    port             INTEGER NOT NULL DEFAULT 0,
                    model_base_dir   TEXT    NOT NULL DEFAULT '',
                    model_type       INTEGER NOT NULL DEFAULT 0,
                    conf_threshold   REAL    NOT NULL DEFAULT 0.5,
                    use_cuda         INTEGER NOT NULL DEFAULT 1,
                    save_dir         TEXT    NOT NULL DEFAULT '',
                    save_pre_ms      INTEGER NOT NULL DEFAULT 5000,
                    save_post_ms     INTEGER NOT NULL DEFAULT 5000,
                    save_cooldown_ms INTEGER NOT NULL DEFAULT 3000,
                    jpeg_quality     INTEGER NOT NULL DEFAULT 80
                );

                CREATE TABLE IF NOT EXISTS global_setting (
                    id              INTEGER PRIMARY KEY AUTOINCREMENT,
                    log_dir         TEXT    NOT NULL DEFAULT '',
                    auto_start      INTEGER NOT NULL DEFAULT 0,
                    language        INTEGER NOT NULL DEFAULT 0,
                    disk_threshold  INTEGER NOT NULL DEFAULT 80
                );
                """;
            cmd.ExecuteNonQuery();
        }

        public void Dispose()
        {
            _connection.Dispose();
        }
    }
}
