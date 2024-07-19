namespace DistributedMessanger
{
    public class TellMessage : Message
    {
        public override MessageType MessageType => MessageType.Tell;
    }
}
