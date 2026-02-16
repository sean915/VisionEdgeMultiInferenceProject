using InferenceClientUI.Models.Entities;
using Microsoft.Data.Sqlite;
using System.Collections.Generic;

namespace InferenceClientUI.Services.Database
{
    /// <summary>
    /// client_setting 테이블 CRUD.
    /// C++ InferenceClientSqliteRepository 역할.
    /// </summary>
    public sealed class ClientSettingRepository
    {
        private readonly SqliteConnection _db;

        public ClientSettingRepository(SqliteConnection db) => _db = db;

        public long Insert(ClientSettingEntity e)
        {
            using var cmd = _db.CreateCommand();
            cmd.CommandText = """
                INSERT INTO client_setting
                    (name, cam_path, ip, port, model_base_dir, model_type,
                     conf_threshold, use_cuda, save_dir, save_pre_ms,
                     save_post_ms, save_cooldown_ms, jpeg_quality)
                VALUES
                    ($name, $cam, $ip, $port, $modelDir, $modelType,
                     $conf, $cuda, $saveDir, $pre, $post, $cool, $jpg);
                """;
            BindParams(cmd, e);
            cmd.ExecuteNonQuery();

            cmd.Parameters.Clear();
            cmd.CommandText = "SELECT last_insert_rowid();";
            return (long)cmd.ExecuteScalar()!;
        }

        public bool Update(ClientSettingEntity e)
        {
            using var cmd = _db.CreateCommand();
            cmd.CommandText = """
                UPDATE client_setting SET
                    name=$name, cam_path=$cam, ip=$ip, port=$port,
                    model_base_dir=$modelDir, model_type=$modelType,
                    conf_threshold=$conf, use_cuda=$cuda, save_dir=$saveDir,
                    save_pre_ms=$pre, save_post_ms=$post,
                    save_cooldown_ms=$cool, jpeg_quality=$jpg
                WHERE id=$id;
                """;
            cmd.Parameters.AddWithValue("$id", e.Id);
            BindParams(cmd, e);
            return cmd.ExecuteNonQuery() > 0;
        }

        public bool Delete(long id)
        {
            using var cmd = _db.CreateCommand();
            cmd.CommandText = "DELETE FROM client_setting WHERE id=$id;";
            cmd.Parameters.AddWithValue("$id", id);
            return cmd.ExecuteNonQuery() > 0;
        }

        public ClientSettingEntity? FindById(long id)
        {
            using var cmd = _db.CreateCommand();
            cmd.CommandText = "SELECT * FROM client_setting WHERE id=$id;";
            cmd.Parameters.AddWithValue("$id", id);
            using var r = cmd.ExecuteReader();
            return r.Read() ? ReadRow(r) : null;
        }

        public List<ClientSettingEntity> FindAll()
        {
            var list = new List<ClientSettingEntity>();
            using var cmd = _db.CreateCommand();
            cmd.CommandText = "SELECT * FROM client_setting ORDER BY id;";
            using var r = cmd.ExecuteReader();
            while (r.Read()) list.Add(ReadRow(r));
            return list;
        }

        private static void BindParams(SqliteCommand cmd, ClientSettingEntity e)
        {
            cmd.Parameters.AddWithValue("$name", e.Name);
            cmd.Parameters.AddWithValue("$cam", e.CamPath);
            cmd.Parameters.AddWithValue("$ip", e.Ip);
            cmd.Parameters.AddWithValue("$port", e.Port);
            cmd.Parameters.AddWithValue("$modelDir", e.ModelBaseDirPath);
            cmd.Parameters.AddWithValue("$modelType", e.ModelType);
            cmd.Parameters.AddWithValue("$conf", e.ConfThreshold);
            cmd.Parameters.AddWithValue("$cuda", e.UseCuda ? 1 : 0);
            cmd.Parameters.AddWithValue("$saveDir", e.SaveDir);
            cmd.Parameters.AddWithValue("$pre", e.SavePreMs);
            cmd.Parameters.AddWithValue("$post", e.SavePostMs);
            cmd.Parameters.AddWithValue("$cool", e.SaveCooldownMs);
            cmd.Parameters.AddWithValue("$jpg", e.JpegQuality);
        }

        private static ClientSettingEntity ReadRow(SqliteDataReader r) => new()
        {
            Id = r.GetInt64(r.GetOrdinal("id")),
            Name = r.GetString(r.GetOrdinal("name")),
            CamPath = r.GetString(r.GetOrdinal("cam_path")),
            Ip = r.GetString(r.GetOrdinal("ip")),
            Port = r.GetInt32(r.GetOrdinal("port")),
            ModelBaseDirPath = r.GetString(r.GetOrdinal("model_base_dir")),
            ModelType = r.GetInt32(r.GetOrdinal("model_type")),
            ConfThreshold = r.GetFloat(r.GetOrdinal("conf_threshold")),
            UseCuda = r.GetInt32(r.GetOrdinal("use_cuda")) != 0,
            SaveDir = r.GetString(r.GetOrdinal("save_dir")),
            SavePreMs = r.GetInt32(r.GetOrdinal("save_pre_ms")),
            SavePostMs = r.GetInt32(r.GetOrdinal("save_post_ms")),
            SaveCooldownMs = r.GetInt32(r.GetOrdinal("save_cooldown_ms")),
            JpegQuality = r.GetInt32(r.GetOrdinal("jpeg_quality")),
        };
    }
}
