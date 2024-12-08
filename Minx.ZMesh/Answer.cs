namespace Minx.ZMesh
{
    public struct Answer
    {
        public string ContentType { get; set; }
        public string Content { get; set; }

        public override string ToString()
        {
            return $"{ContentType}: {Content}";
        }
    }
}