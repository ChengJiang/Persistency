class PersistDependency {
    private:
        void* persistBufferId;
        int persistId;
    public:
        PersistDependency();
        PersistDependency(void* _persistBufferId, int _persistId);
        ~PersistDependency();

        void setPersistBufferId(void* _persistBufferId);

        void setPersistId(int _persistId);

        void* getPersistBufferId();

        int getPersistId();
};
